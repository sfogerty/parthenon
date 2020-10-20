//========================================================================================
// (C) (or copyright) 2020. Triad National Security, LLC. All rights reserved.
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

#include "state_descriptor.hpp"

bool parthenon::StateDescriptor::AddField(const std::string &field_name, Metadata &m,
                                          DerivedOwnership owner) {
  if (m.IsSet(Metadata::Sparse)) {
    auto miter = sparseMetadataMap_.find(field_name);
    if (miter != sparseMetadataMap_.end()) {
      miter->second.push_back(m);
    } else {
      sparseMetadataMap_[field_name] = {m};
    }
  } else {
    const std::string &assoc = m.getAssociated();
    if (!assoc.length()) m.Associate(field_name);
    auto miter = metadataMap_.find(field_name);
    if (miter != metadataMap_.end()) { // this field has already been added
      Metadata &mprev = miter->second;
      PARTHENON_REQUIRE(owner != DerivedOwnership::unique,
                        "Field " + field_name +
                            " add with DerivedOwnership::unique already exists");
      PARTHENON_REQUIRE(mprev == m, "Field " + field_name +
                                        " already exists with different metadata");
      return false;
    } else {
      metadataMap_[field_name] = m;
      m.Associate("");
    }
  }
  return true;
}
