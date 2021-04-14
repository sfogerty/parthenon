//========================================================================================
// Athena++ astrophysical MHD code
// Copyright(C) 2014 James M. Stone <jmstone@princeton.edu> and other code contributors
// Licensed under the 3-clause BSD License, see LICENSE file for details
//========================================================================================
// (C) (or copyright) 2020-2021. Triad National Security, LLC. All rights reserved.
//
// This program was produced under U.S. Government contract 89233218CNA000001 for Los
// Alamos National Laboratory (LANL), which is operated by Triad National Security, LLC
// for the U.S. Department of Energy/National Nuclear Security Administration. All rights
// in the program are reserved by Triad National Security, LLC, and the U.S. Department
// of Energy/National Nuclear Security Administration. The Government is granted for
// itself and others acting on its behalf a nonexclusive, paid-up, irrevocable worldwide
// license in this material to reproduce, prepare derivative works, distribute copies to
// the public, perform publicly and display publicly, and to permit others to do so.
//========================================================================================

// options for building
#include "H5Tpublic.h"
#include "config.hpp"
#include "interface/metadata.hpp"

// Only proceed if HDF5 output enabled
#ifdef HDF5OUTPUT

#include <algorithm>
#include <memory>
#include <set>
#include <type_traits>
#include <unordered_map>

#include "mesh/meshblock.hpp"
#include "outputs/parthenon_hdf5.hpp"

namespace parthenon {

using namespace HDF5;

// Helper struct containing some information about a variable that we can easily
// communicate via MPI
struct VarInfo {
  // We need to communicate this struct via MPI. To Make our lives a bit easier, we will
  // combine the vlen integer and the is_sparse and is_vector flags into a single int
  // (call it info_code) and communicate that.
  //
  // The info_code will have the vlen in the lower 16 bits and bits 20 and 21 will encode
  // the is_sparse and is_vector flags
  static constexpr int max_vlen = (1 << 16) - 1;
  static constexpr int sparse_flag = (1 << 20);
  static constexpr int vector_flag = (1 << 21);

  std::string label;
  int vlen;
  bool is_sparse;
  bool is_vector;

 private:
  VarInfo() = default;

 public:
  static VarInfo Decode(const std::string &label, int info_code) {
    VarInfo res;
    res.label = label;
    res.vlen = info_code & max_vlen;
    res.is_sparse = (info_code & sparse_flag) > 0;
    res.is_vector = (info_code & vector_flag) > 0;

    return res;
  }

  explicit VarInfo(const std::shared_ptr<CellVariable<Real>> &var)
      : label(var->label()), vlen(var->GetDim(4)), is_sparse(var->IsSparse()),
        is_vector(var->IsSet(Metadata::Vector)) {
    if ((vlen <= 0) || (vlen > max_vlen)) {
      std::stringstream msg;
      msg << "### ERROR: Got variable " << label << " with length " << vlen
          << ". vlen must be between 0 and " << max_vlen << std::endl;
      PARTHENON_FAIL(msg);
    }
  }

  int get_info_code() const {
    int code = vlen;
    if (is_sparse) code += sparse_flag;
    if (is_vector) code += vector_flag;

    return code;
  }

  // so we can put VarInfo into a set
  bool operator<(const VarInfo &other) const {
    if ((label == other.label) && (vlen != other.vlen)) {
      // variables with the same label must have the same lengths
      std::stringstream msg;
      msg << "### ERROR: Got variable " << label << " with multiple different lengths"
          << std::endl;
      PARTHENON_FAIL(msg);
    }

    return label < other.label;
  }
};

// XDMF subroutine to write a dataitem that refers to an HDF array
static std::string stringXdmfArrayRef(const std::string &prefix,
                                      const std::string &hdfPath,
                                      const std::string &label, const hsize_t *dims,
                                      const int &ndims, const std::string &theType,
                                      const int &precision) {
  std::string mystr =
      prefix + R"(<DataItem Format="HDF" Dimensions=")" + std::to_string(dims[0]);
  for (int i = 1; i < ndims; i++)
    mystr += " " + std::to_string(dims[i]);
  mystr += "\" Name=\"" + label + "\"";
  mystr += " NumberType=\"" + theType + "\"";
  mystr += R"( Precision=")" + std::to_string(precision) + R"(">)" + '\n';
  mystr += prefix + "  " + hdfPath + label + "</DataItem>" + '\n';
  return mystr;
}

static void writeXdmfArrayRef(std::ofstream &fid, const std::string &prefix,
                              const std::string &hdfPath, const std::string &label,
                              const hsize_t *dims, const int &ndims,
                              const std::string &theType, const int &precision) {
  fid << stringXdmfArrayRef(prefix, hdfPath, label, dims, ndims, theType, precision)
      << std::flush;
}

static void writeXdmfSlabVariableRef(std::ofstream &fid, const std::string &name,
                                     std::string &hdfFile, int iblock, const int &vlen,
                                     int &ndims, hsize_t *dims,
                                     const std::string &dims321, bool isVector) {
  // writes a slab reference to file

  std::vector<std::string> names;
  int nentries = 1;
  int vector_size = 1;
  if (vlen == 1 || isVector) {
    names.push_back(name);
  } else {
    nentries = vlen;
    for (int i = 0; i < vlen; i++) {
      names.push_back(name + "_" + std::to_string(i));
    }
  }
  if (isVector) vector_size = vlen;

  const std::string prefix = "      ";
  for (int i = 0; i < nentries; i++) {
    fid << prefix << R"(<Attribute Name=")" << names[i] << R"(" Center="Cell")";
    if (isVector) {
      fid << R"( AttributeType="Vector")"
          << R"( Dimensions=")" << dims321 << " " << vector_size << R"(")";
    }
    fid << ">" << std::endl;
    fid << prefix << "  "
        << R"(<DataItem ItemType="HyperSlab" Dimensions=")" << dims321 << " "
        << vector_size << R"(">)" << std::endl;
    fid << prefix << "    "
        << R"(<DataItem Dimensions="3 5" NumberType="Int" Format="XML">)" << iblock
        << " 0 0 0 " << i << " 1 1 1 1 1 1 " << dims321 << " " << vector_size
        << "</DataItem>" << std::endl;
    writeXdmfArrayRef(fid, prefix + "    ", hdfFile + ":/", name, dims, ndims, "Float",
                      8);
    fid << prefix << "  "
        << "</DataItem>" << std::endl;
    fid << prefix << "</Attribute>" << std::endl;
  }
  return;
}

void genXDMF(std::string hdfFile, Mesh *pm, SimTime *tm, int nx1, int nx2, int nx3,
             const std::set<VarInfo> &var_list) {
  // using round robin generation.
  // must switch to MPIIO at some point

  // only rank 0 writes XDMF
  if (Globals::my_rank != 0) {
    return;
  }
  std::string filename_aux = hdfFile + ".xdmf";
  std::ofstream xdmf;
  hsize_t dims[5] = {0, 0, 0, 0, 0};

  // open file
  xdmf = std::ofstream(filename_aux.c_str(), std::ofstream::trunc);

  // Write header
  xdmf << R"(<?xml version="1.0" ?>)" << std::endl;
  xdmf << R"(<!DOCTYPE Xdmf SYSTEM "Xdmf.dtd">)" << std::endl;
  xdmf << R"(<Xdmf Version="3.0">)" << std::endl;
  xdmf << "  <Domain>" << std::endl;
  xdmf << R"(  <Grid Name="Mesh" GridType="Collection">)" << std::endl;
  if (tm != nullptr) {
    xdmf << R"(    <Time Value=")" << tm->time << R"("/>)" << std::endl;
    xdmf << R"(    <Information Name="Cycle" Value=")" << tm->ncycle << R"("/>)"
         << std::endl;
  }

  std::string blockTopology = R"(      <Topology Type="3DRectMesh" NumberOfElements=")" +
                              std::to_string(nx3 + 1) + " " + std::to_string(nx2 + 1) +
                              " " + std::to_string(nx1 + 1) + R"("/>)" + '\n';
  const std::string slabPreDim = R"(        <DataItem ItemType="HyperSlab" Dimensions=")";
  const std::string slabPreBlock2D =
      R"("><DataItem Dimensions="3 2" NumberType="Int" Format="XML">)";
  const std::string slabTrailer = "</DataItem>";

  // Now write Grid for each block
  dims[0] = pm->nbtotal;
  std::string dims321 =
      std::to_string(nx3) + " " + std::to_string(nx2) + " " + std::to_string(nx1);

  int ndims = 5;

  for (int ib = 0; ib < pm->nbtotal; ib++) {
    xdmf << "    <Grid GridType=\"Uniform\" Name=\"" << ib << "\">" << std::endl;
    xdmf << blockTopology;
    xdmf << R"(      <Geometry Type="VXVYVZ">)" << std::endl;
    xdmf << slabPreDim << nx1 + 1 << slabPreBlock2D << ib << " 0 1 1 1 " << nx1 + 1
         << slabTrailer << std::endl;

    dims[1] = nx1 + 1;
    writeXdmfArrayRef(xdmf, "          ", hdfFile + ":/Locations/", "x", dims, 2, "Float",
                      8);
    xdmf << "</DataItem>" << std::endl;

    xdmf << slabPreDim << nx2 + 1 << slabPreBlock2D << ib << " 0 1 1 1 " << nx2 + 1
         << slabTrailer << std::endl;

    dims[1] = nx2 + 1;
    writeXdmfArrayRef(xdmf, "          ", hdfFile + ":/Locations/", "y", dims, 2, "Float",
                      8);
    xdmf << "</DataItem>" << std::endl;

    xdmf << slabPreDim << nx3 + 1 << slabPreBlock2D << ib << " 0 1 1 1 " << nx3 + 1
         << slabTrailer << std::endl;

    dims[1] = nx3 + 1;
    writeXdmfArrayRef(xdmf, "          ", hdfFile + ":/Locations/", "z", dims, 2, "Float",
                      8);
    xdmf << "</DataItem>" << std::endl;

    xdmf << "      </Geometry>" << std::endl;

    // write graphics variables
    dims[1] = nx3;
    dims[2] = nx2;
    dims[3] = nx1;
    dims[4] = 1;
    for (auto &vinfo : var_list) {
      const int vlen = vinfo.vlen;
      dims[4] = vlen;
      writeXdmfSlabVariableRef(xdmf, vinfo.label, hdfFile, ib, vlen, ndims, dims, dims321,
                               vinfo.is_vector);
    }
    xdmf << "      </Grid>" << std::endl;
  }
  xdmf << "    </Grid>" << std::endl;
  xdmf << "  </Domain>" << std::endl;
  xdmf << "</Xdmf>" << std::endl;
  xdmf.close();

  return;
}

void PHDF5Output::WriteOutputFile(Mesh *pm, ParameterInput *pin, SimTime *tm) {
  if (output_params.single_precision_output) {
    this->template WriteOutputFileImpl<true>(pm, pin, tm);
  } else {
    this->template WriteOutputFileImpl<false>(pm, pin, tm);
  }
}

//----------------------------------------------------------------------------------------
//! \fn void PHDF5Output:::WriteOutputFileImpl(Mesh *pm, ParameterInput *pin, bool flag)
//  \brief Cycles over all MeshBlocks and writes OutputData in the Parthenon HDF5 format,
//         one file per output using parallel IO.
template <bool WRITE_SINGLE_PRECISION>
void PHDF5Output::WriteOutputFileImpl(Mesh *pm, ParameterInput *pin, SimTime *tm) {
  // writes all graphics variables to hdf file
  // HDF5 structures
  // Also writes companion xdmf file

  const int max_blocks_global = pm->nbtotal;
  const int num_blocks_local = static_cast<int>(pm->block_list.size());

  const IndexDomain theDomain =
      (output_params.include_ghost_zones ? IndexDomain::entire : IndexDomain::interior);

  auto const &first_block = *(pm->block_list.front());

  // shooting a blank just for getting the variable names
  const IndexRange out_ib = first_block.cellbounds.GetBoundsI(theDomain);
  const IndexRange out_jb = first_block.cellbounds.GetBoundsJ(theDomain);
  const IndexRange out_kb = first_block.cellbounds.GetBoundsK(theDomain);

  auto const nx1 = out_ib.e - out_ib.s + 1;
  auto const nx2 = out_jb.e - out_jb.s + 1;
  auto const nx3 = out_kb.e - out_kb.s + 1;

  const int rootLevel = pm->GetRootLevel();
  const int max_level = pm->GetCurrentLevel() - rootLevel;
  const auto &nblist = pm->GetNbList();

  // open HDF5 file
  // Define output filename
  auto filename = std::string(output_params.file_basename);
  filename.append(".");
  filename.append(output_params.file_id);
  filename.append(".");
  std::stringstream file_number;
  file_number << std::setw(5) << std::setfill('0') << output_params.file_number;
  filename.append(file_number.str());
  filename.append(restart_ ? ".rhdf" : ".phdf");

  // set file access property list
#ifdef MPI_PARALLEL
  /* set the file access template for parallel IO access */
  H5P const acc_file = H5P::FromHIDCheck(H5Pcreate(H5P_FILE_ACCESS));

  /* ---------------------------------------------------------------------
     platform dependent code goes here -- the access template must be
     tuned for a particular filesystem blocksize.  some of these
     numbers are guesses / experiments, others come from the file system
     documentation.

     The sieve_buf_size should be equal a multiple of the disk block size
     ---------------------------------------------------------------------- */

  /* create an MPI_INFO object -- on some platforms it is useful to
     pass some information onto the underlying MPI_File_open call */
  MPI_Info FILE_INFO_TEMPLATE;
  PARTHENON_MPI_CHECK(MPI_Info_create(&FILE_INFO_TEMPLATE));

  // Free MPI_Info on error on return or throw
  struct MPI_InfoDeleter {
    MPI_Info info;
    ~MPI_InfoDeleter() { MPI_Info_free(&info); }
  } delete_info{FILE_INFO_TEMPLATE};

  PARTHENON_HDF5_CHECK(H5Pset_sieve_buf_size(acc_file, 262144));
  PARTHENON_HDF5_CHECK(H5Pset_alignment(acc_file, 524288, 262144));

  PARTHENON_MPI_CHECK(MPI_Info_set(FILE_INFO_TEMPLATE, "access_style", "write_once"));
  PARTHENON_MPI_CHECK(MPI_Info_set(FILE_INFO_TEMPLATE, "collective_buffering", "true"));
  PARTHENON_MPI_CHECK(MPI_Info_set(FILE_INFO_TEMPLATE, "cb_block_size", "1048576"));
  PARTHENON_MPI_CHECK(MPI_Info_set(FILE_INFO_TEMPLATE, "cb_buffer_size", "4194304"));

  /* tell the HDF5 library that we want to use MPI-IO to do the writing */
  PARTHENON_HDF5_CHECK(H5Pset_fapl_mpio(acc_file, MPI_COMM_WORLD, FILE_INFO_TEMPLATE));
  PARTHENON_HDF5_CHECK(H5Pset_fapl_mpio(acc_file, MPI_COMM_WORLD, MPI_INFO_NULL));
#else
  hid_t const acc_file = H5P_DEFAULT;
#endif // ifdef MPI_PARALLEL

  // now open the file
  H5F const file = H5F::FromHIDCheck(
      H5Fcreate(filename.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, acc_file));

  // -------------------------------------------------------------------------------- //
  //   WRITING ATTRIBUTES                                                             //
  // -------------------------------------------------------------------------------- //

  if (restart_) {
    // write input key-value pairs
    std::ostringstream oss;
    pin->ParameterDump(oss);

    // Mesh information
    const H5G input_group = MakeGroup(file, "/Input");

    WriteHDF5Attribute("File", oss.str().c_str(), input_group);
  } // Input section

  // write timestep relevant attributes
  {
    // attributes written here:
    // All ranks write attributes
    const H5G info_group = MakeGroup(file, "/Info");

    if (tm != nullptr) {
      WriteHDF5Attribute("NCycle", tm->ncycle, info_group);
      WriteHDF5Attribute("Time", tm->time, info_group);
      WriteHDF5Attribute("dt", tm->dt, info_group);
    }
    WriteHDF5Attribute("NumDims", pm->ndim, info_group);
    WriteHDF5Attribute("NumMeshBlocks", pm->nbtotal, info_group);
    WriteHDF5Attribute("MaxLevel", max_level, info_group);
    // write whether we include ghost cells or not
    WriteHDF5Attribute("IncludesGhost", output_params.include_ghost_zones ? 1 : 0,
                       info_group);
    // write number of ghost cells in simulation
    WriteHDF5Attribute("NGhost", Globals::nghost, info_group);
    WriteHDF5Attribute("Coordinates", std::string(first_block.coords.Name()).c_str(),
                       info_group);

    hsize_t nPE = Globals::nranks;
    WriteHDF5Attribute("BlocksPerPE", nblist, info_group);

    // write mesh block size
    WriteHDF5Attribute("MeshBlockSize", std::vector<int>{nx1, nx2, nx3}, info_group);
  } // Info section

  // Mesh information
  if (restart_) {
    const H5G mesh_group = MakeGroup(file, "/Mesh");
    WriteHDF5Attribute("blockSize",
                       std::vector<int>{first_block.block_size.nx1,
                                        first_block.block_size.nx2,
                                        first_block.block_size.nx3},
                       mesh_group);
    WriteHDF5Attribute("includesGhost", output_params.include_ghost_zones ? 1 : 0,
                       mesh_group);
    WriteHDF5Attribute("nbtotal", pm->nbtotal, mesh_group);
    WriteHDF5Attribute("nbnew", pm->nbnew, mesh_group);
    WriteHDF5Attribute("nbdel", pm->nbdel, mesh_group);
    WriteHDF5Attribute("rootLevel", rootLevel, mesh_group);
    WriteHDF5Attribute("MaxLevel", max_level, mesh_group);
    WriteHDF5Attribute("refine", pm->adaptive ? 1 : 0, mesh_group);
    WriteHDF5Attribute("multilevel", pm->multilevel ? 1 : 0, mesh_group);

    // mesh bounds
    const auto &rs = pm->mesh_size;
    WriteHDF5Attribute(
        "bounds",
        std::vector<Real>{rs.x1min, rs.x2min, rs.x3min, rs.x1max, rs.x2max, rs.x3max},
        mesh_group);

    WriteHDF5Attribute("ratios", std::vector<Real>{rs.x1rat, rs.x2rat, rs.x3rat},
                       mesh_group);

    // boundary conditions
    std::vector<int> bcsi(6);
    for (int ib = 0; ib < 6; ib++) {
      bcsi[ib] = static_cast<int>(pm->mesh_bcs[ib]);
    }
    WriteHDF5Attribute("bc", bcsi, mesh_group);
  } // Mesh section

  // -------------------------------------------------------------------------------- //
  //   WRITING MESHBLOCK METADATA                                                     //
  // -------------------------------------------------------------------------------- //

  // set local offset, always the same for all data sets
  hsize_t my_offset = 0;
  for (int i = 0; i < Globals::my_rank; i++) {
    my_offset += nblist[i];
  }

  const std::array<hsize_t, 5> local_offset({my_offset, 0, 0, 0, 0});

  // these can vary by data set, except index 0 is always the same
  std::array<hsize_t, 5> local_count(
      {static_cast<hsize_t>(num_blocks_local), 1, 1, 1, 1});
  std::array<hsize_t, 5> global_count(
      {static_cast<hsize_t>(max_blocks_global), 1, 1, 1, 1});

  // for convenience
  const hsize_t *const p_loc_offset = local_offset.data();
  const hsize_t *const p_loc_cnt = local_count.data();
  const hsize_t *const p_glob_cnt = global_count.data();

  H5P const pl_xfer = H5P::FromHIDCheck(H5Pcreate(H5P_DATASET_XFER));
  H5P const pl_dcreate = H5P::FromHIDCheck(H5Pcreate(H5P_DATASET_CREATE));
  {
    const std::array<hsize_t, 5> chunk_size({1, static_cast<hsize_t>(nx3),
                                             static_cast<hsize_t>(nx2),
                                             static_cast<hsize_t>(nx1), 1});

    PARTHENON_HDF5_CHECK(H5Pset_chunk(pl_dcreate, 5, chunk_size.data()));

    if (HDF5_COMPRESSION_LEVEL > 0) {
      PARTHENON_HDF5_CHECK(
          H5Pset_deflate(pl_dcreate, std::min(9, HDF5_COMPRESSION_LEVEL)));
    }
  }

#ifdef MPI_PARALLEL
  PARTHENON_HDF5_CHECK(H5Pset_dxpl_mpio(pl_xfer, H5FD_MPIO_COLLECTIVE));
#endif

  // write Blocks restart metadata
  if (restart_) {
    const H5G gBlocks = MakeGroup(file, "/Blocks");

    // write Xmin[ndim] for blocks
    {
      std::vector<Real> tmpData(num_blocks_local * 3);
      int i = 0;

      for (auto &pmb : pm->block_list) {
        auto xmin = pmb->coords.GetXmin();
        tmpData[i++] = xmin[0];
        if (pm->ndim > 1) {
          tmpData[i++] = xmin[1];
        }
        if (pm->ndim > 2) {
          tmpData[i++] = xmin[2];
        }
      }
      local_count[1] = global_count[1] = pm->ndim;
      HDF5Write2D(gBlocks, "xmin", tmpData.data(), p_loc_offset, p_loc_cnt, p_glob_cnt,
                  pl_xfer);
    }

    // write Block ID
    {
      // LOC.lx1,2,3
      hsize_t n = 3;
      std::vector<int64_t> tmpLoc(num_blocks_local * n);
      local_count[1] = global_count[1] = n;

      int i = 0;
      for (auto &pmb : pm->block_list) {
        tmpLoc[i++] = pmb->loc.lx1;
        tmpLoc[i++] = pmb->loc.lx2;
        tmpLoc[i++] = pmb->loc.lx3;
      }
      HDF5Write2D(gBlocks, "loc.lx123", tmpLoc.data(), p_loc_offset, p_loc_cnt,
                  p_glob_cnt, pl_xfer);

      // (LOC.)level, GID, LID, cnghost, gflag
      n = 5;
      std::vector<int> tmpID(num_blocks_local * n);
      local_count[1] = global_count[1] = n;

      i = 0;
      for (auto &pmb : pm->block_list) {
        tmpID[i++] = pmb->loc.level;
        tmpID[i++] = pmb->gid;
        tmpID[i++] = pmb->lid;
        tmpID[i++] = pmb->cnghost;
        tmpID[i++] = pmb->gflag;
      }
      HDF5Write2D(gBlocks, "loc.level-gid-lid-cnghost-gflag", tmpID.data(), p_loc_offset,
                  p_loc_cnt, p_glob_cnt, pl_xfer);
    }
  } // Block section

  // Write mesh coordinates to file
  if (!restart_) {
    // set starting point in hyperslab for our blocks and
    // number of blocks on our PE

    // open locations tab
    const H5G gLocations = MakeGroup(file, "/Locations");

    // write X coordinates
    std::vector<Real> loc_x((nx1 + 1) * num_blocks_local);
    std::vector<Real> loc_y((nx2 + 1) * num_blocks_local);
    std::vector<Real> loc_z((nx3 + 1) * num_blocks_local);

    size_t idx_x = 0;
    size_t idx_y = 0;
    size_t idx_z = 0;

    for (size_t b = 0; b < pm->block_list.size(); ++b) {
      auto &pmb = pm->block_list[b];

      for (int i = out_ib.s; i <= out_ib.e + 1; ++i) {
        loc_x[idx_x++] = pmb->coords.x1f(0, 0, i);
      }

      for (int j = out_jb.s; j <= out_jb.e + 1; ++j) {
        loc_y[idx_y++] = pmb->coords.x2f(0, j, 0);
      }

      for (int k = out_kb.s; k <= out_kb.e + 1; ++k) {
        loc_z[idx_z++] = pmb->coords.x3f(k, 0, 0);
      }
    }

    local_count[1] = global_count[1] = nx1 + 1;
    HDF5Write2D(gLocations, "x", loc_x.data(), p_loc_offset, p_loc_cnt, p_glob_cnt,
                pl_xfer);

    local_count[1] = global_count[1] = nx2 + 1;
    HDF5Write2D(gLocations, "y", loc_y.data(), p_loc_offset, p_loc_cnt, p_glob_cnt,
                pl_xfer);

    local_count[1] = global_count[1] = nx3 + 1;
    HDF5Write2D(gLocations, "z", loc_z.data(), p_loc_offset, p_loc_cnt, p_glob_cnt,
                pl_xfer);
  } // Locations section

  // -------------------------------------------------------------------------------- //
  //   WRITING VARIABLES DATA                                                         //
  // -------------------------------------------------------------------------------- //

  // first we need to get list of variables, because sparse variables are only
  // expanded on some blocks, we need to look at the list of variables on each block
  // combine these into a global list of variables

  auto get_MeshBlockDataIterator = [=](const std::shared_ptr<MeshBlock> pmb) {
    if (restart_) {
      return MeshBlockDataIterator<Real>(
          pmb->meshblock_data.Get(),
          {parthenon::Metadata::Independent, parthenon::Metadata::Restart}, true);
    } else {
      return MeshBlockDataIterator<Real>(pmb->meshblock_data.Get(),
                                         output_params.variables);
    }
  };

  // local list of unique vars
  std::set<VarInfo> all_unique_vars;
  for (auto &pmb : pm->block_list) {
    auto ci = get_MeshBlockDataIterator(pmb);
    for (auto &v : ci.vars) {
      VarInfo vinfo(v);
      all_unique_vars.insert(vinfo);
    }
  }

#ifdef MPI_PARALLEL
  {
    // we need to do a global allgather to get the global list of unique variables to
    // be written to the HDF5 file

    // the label buffer contains all labels of the unique variables on this rank
    // separated by \t, e.g.: "label0\tlabel1\tlabel2\t"
    std::string label_buffer;
    std::vector<int> code_buffer(all_unique_vars.size(), 0);

    size_t idx = 0;
    for (const auto &vi : all_unique_vars) {
      label_buffer += vi.label + "\t";
      code_buffer[idx++] = vi.get_info_code();
    }

    // first we need to communicate the lengths of the label_buffer and vlen_buffer to
    // all ranks, 2 ints per rank: first int: label_buffer length, second int:
    // vlen_buffer length
    std::vector<int> buffer_lengths(2 * Globals::nranks, 0);
    buffer_lengths[Globals::my_rank * 2 + 0] = static_cast<int>(label_buffer.size());
    buffer_lengths[Globals::my_rank * 2 + 1] = static_cast<int>(code_buffer.size());

    PARTHENON_MPI_CHECK(MPI_Allgather(MPI_IN_PLACE, 2, MPI_INT, buffer_lengths.data(), 2,
                                      MPI_INT, MPI_COMM_WORLD));

    // now do an Allgatherv combining label_buffer and vlen_buffer from all ranks
    std::vector<int> label_lengths(Globals::nranks, 0);
    std::vector<int> label_offsets(Globals::nranks, 0);
    std::vector<int> code_lengths(Globals::nranks, 0);
    std::vector<int> code_offsets(Globals::nranks, 0);

    int label_offset = 0;
    int code_offset = 0;
    for (int n = 0; n < Globals::nranks; ++n) {
      label_offsets[n] = label_offset;
      code_offsets[n] = code_offset;

      label_lengths[n] = buffer_lengths[n * 2 + 0];
      code_lengths[n] = buffer_lengths[n * 2 + 1];

      label_offset += label_lengths[n];
      code_offset += code_lengths[n];
    }

    // result buffers with global data
    std::vector<char> all_labels_buffer(label_offset, '\0');
    std::vector<int> all_codes(code_offset, 0);

    // fill in our values in global buffers
    memcpy(all_labels_buffer.data() + label_offsets[Globals::my_rank],
           label_buffer.data(), label_buffer.size() * sizeof(char));
    memcpy(all_codes.data() + code_offsets[Globals::my_rank], code_buffer.data(),
           code_buffer.size() * sizeof(int));

    PARTHENON_MPI_CHECK(MPI_Allgatherv(
        MPI_IN_PLACE, label_lengths[Globals::my_rank], MPI_BYTE, all_labels_buffer.data(),
        label_lengths.data(), label_offsets.data(), MPI_BYTE, MPI_COMM_WORLD));

    PARTHENON_MPI_CHECK(MPI_Allgatherv(MPI_IN_PLACE, code_lengths[Globals::my_rank],
                                       MPI_INT, all_codes.data(), code_lengths.data(),
                                       code_offsets.data(), MPI_INT, MPI_COMM_WORLD));

    // unpack labels
    std::vector<std::string> all_labels;
    const char *curr = all_labels_buffer.data();
    const char *const end = curr + all_labels_buffer.size();

    while (curr < end) {
      const auto tab = strchr(curr, '\t');
      if (tab == nullptr) {
        std::stringstream msg;
        msg << "### ERROR: all_labels_buffer does not end with \\t" << std::endl;
        PARTHENON_FAIL(msg);
      }

      if (tab == curr) {
        std::stringstream msg;
        msg << "### ERROR: Got an empty label" << std::endl;
        PARTHENON_FAIL(msg);
      }

      std::string label(curr, tab - curr);
      all_labels.push_back(label);
      curr = tab + 1;
    }

    if (all_labels.size() != all_codes.size()) {
      printf("all_labels: %zu\n", all_labels.size());
      for (size_t i = 0; i < all_labels.size(); ++i)
        printf("%4zu: %s\n", i, all_labels[i].c_str());
      printf("all_codes: %zu\n", all_codes.size());
      for (size_t i = 0; i < all_codes.size(); ++i)
        printf("%4zu: %i\n", i, all_codes[i]);

      std::stringstream msg;
      msg << "### ERROR: all_labels and all_codes have different sizes" << std::endl;
      PARTHENON_FAIL(msg);
    }

    // finally make list of all unique variables
    for (size_t i = 0; i < all_labels.size(); ++i) {
      all_unique_vars.insert(VarInfo::Decode(all_labels[i], all_codes[i]));
    }
  }
#endif

  // We need to add information about the sparse variables to the HDF5 file, namely:
  // 1) Which variables are sparse
  // 2) Is a sparse id of a particular sparse variable expanded on a given block
  //
  // This information is stored in the dataset called "SparseInfo". The data set
  // contains an attribute "SparseFields" that is a vector of strings with the names
  // of the sparse fields (field name with sparse id, i.e. "bar_28", "bar_7", foo_1",
  // "foo_145"). The field names are in alphabetical order, which is the same order
  // they show up in all_unique_vars (because it's a sorted set).
  //
  // The dataset SparseInfo itself is a 2D array of bools. The first index is the
  // global block index and the second index is the sparse field (same order as the
  // SparseFields attribute). SparseInfo[b][v] is true if the sparse field with index
  // v is expanded on the block with index b, otherwise the value is false

  std::vector<std::string> sparse_names;
  std::unordered_map<std::string, size_t> sparse_field_idx;
  for (auto &vinfo : all_unique_vars) {
    if (vinfo.is_sparse) {
      sparse_field_idx.insert({vinfo.label, sparse_names.size()});
      sparse_names.push_back(vinfo.label);
    }
  }

  hsize_t num_sparse = sparse_names.size();
  // can't use std::vector here because std::vector<hbool_t> is the same as
  // std::vector<bool> and it doesn't have .data() member
  std::unique_ptr<hbool_t[]> sparse_expanded(new hbool_t[num_blocks_local * num_sparse]);

  // allocate space for largest size variable
  const hsize_t varSize = nx3 * nx2 * nx1;
  int vlen_max = 0;
  for (auto &vinfo : all_unique_vars) {
    vlen_max = std::max(vlen_max, vinfo.vlen);
  }

  using OutT = typename std::conditional<WRITE_SINGLE_PRECISION, float, Real>::type;
  std::vector<OutT> tmpData(varSize * vlen_max * num_blocks_local);

  // create persistent spaces
  local_count[1] = global_count[1] = nx3;
  local_count[2] = global_count[2] = nx2;
  local_count[3] = global_count[3] = nx1;

  // for each variable we write
  for (auto &vinfo : all_unique_vars) {
    // not really necessary, but doesn't hurt
    memset(tmpData.data(), 0, tmpData.size() * sizeof(OutT));

    const std::string var_name = vinfo.label;
    const hsize_t vlen = vinfo.vlen;

    local_count[4] = global_count[4] = vlen;

    // load up data
    hsize_t index = 0;
    bool found_any = false;

    // for each local mesh block
    for (size_t b_idx = 0; b_idx < num_blocks_local; ++b_idx) {
      const auto &pmb = pm->block_list[b_idx];
      bool found = false;

      // for each variable that this local meshblock actually has
      auto ci = get_MeshBlockDataIterator(pmb);
      for (auto &v : ci.vars) {
        // Note index l transposed to interior
        if (var_name.compare(v->label()) == 0) {
          auto v_h = v->data.GetHostMirrorAndCopy();
          for (int k = out_kb.s; k <= out_kb.e; ++k) {
            for (int j = out_jb.s; j <= out_jb.e; ++j) {
              for (int i = out_ib.s; i <= out_ib.e; ++i) {
                for (int l = 0; l < vlen; ++l) {
                  tmpData[index++] = static_cast<OutT>(v_h(l, k, j, i));
                }
              }
            }
          }

          found = true;
          break;
        }
      }

      if (vinfo.is_sparse) {
        size_t sparse_idx = sparse_field_idx.at(vinfo.label);
        sparse_expanded[b_idx * num_sparse + sparse_idx] = found;
      }

      if (!found) {
        if (vinfo.is_sparse) {
          hsize_t N = varSize * vlen;
          memset(tmpData.data() + index, 0, N * sizeof(OutT));
          index += N;
        } else {
          std::stringstream msg;
          msg << "### ERROR: Unable to find dense variable " << var_name << std::endl;
          PARTHENON_FAIL(msg);
        }
      } else {
        found_any = true;
      }
    }

    // If found_any is true, it means that at least one local block has data for the
    // current variable writing, so we write the tmpData buffer to the HDF5 file. Note,
    // the tmpData buffer may contain some 0's for the local blocks that don't have this
    // variable. It's ok to write these 0's because compression will take care of them.
    // Otherwise, if found_any is false, then none of the local blocks have data for this
    // variable, so we don't need to write a buffer of all 0's.
    if (found_any) {
      // write data to file
      HDF5WriteND(file, var_name, tmpData.data(), 5, p_loc_offset, p_loc_cnt, p_glob_cnt,
                  pl_xfer, pl_dcreate);
    }
  }

  // write SparseInfo and SparseFields
  {
    local_count[1] = global_count[1] = num_sparse;

    HDF5Write2D(file, "SparseInfo", sparse_expanded.get(), p_loc_offset, p_loc_cnt,
                p_glob_cnt, pl_xfer);

    // write names of sparse fields as attribute, first convert to vector of const char*
    std::vector<const char *> names(num_sparse);
    for (size_t i = 0; i < num_sparse; ++i)
      names[i] = sparse_names[i].c_str();

    const H5D dset = H5D::FromHIDCheck(H5Dopen2(file, "SparseInfo", H5P_DEFAULT));
    WriteHDF5Attribute("SparseFields", names, dset);
  } // SparseInfo and SparseFields sections

  if (!restart_) {
    // generate XDMF companion file
    genXDMF(filename, pm, tm, nx1, nx2, nx3, all_unique_vars);
  }

  // advance output parameters
  output_params.file_number++;
  output_params.next_time += output_params.dt;
  pin->SetInteger(output_params.block_name, "file_number", output_params.file_number);
  pin->SetReal(output_params.block_name, "next_time", output_params.next_time);
}

// explicit template instantiation
template void PHDF5Output::WriteOutputFileImpl<false>(Mesh *, ParameterInput *,
                                                      SimTime *);
template void PHDF5Output::WriteOutputFileImpl<true>(Mesh *, ParameterInput *, SimTime *);

} // namespace parthenon

#endif // ifdef HDF5OUTPUT
