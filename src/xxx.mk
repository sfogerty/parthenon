# No hdf5 defined
cd
/Volumes/home/sriram/codes/parthenon/b/src
&&
/usr/local/bin/g++-10
-I/Volumes/home/sriram/codes/parthenon/src
-I/Volumes/home/sriram/codes/parthenon/b/src/generated
-I/Volumes/home/sriram/codes/parthenon/b/Kokkos
-I/Volumes/home/sriram/codes/parthenon/b/Kokkos/core/src
-I/Volumes/home/sriram/codes/parthenon/external/Kokkos/core/src
-I/Volumes/home/sriram/codes/parthenon/b/Kokkos/containers/src
-I/Volumes/home/sriram/codes/parthenon/external/Kokkos/containers/src
-I/Volumes/home/sriram/codes/parthenon/b/Kokkos/algorithms/src
-I/Volumes/home/sriram/codes/parthenon/external/Kokkos/algorithms/src
-isystem
/Volumes/home/sriram/software/Darwin-gcc10/include
-O2
-g
-DNDEBUG
-isysroot
/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.15.sdk
-fopenmp
-o
CMakeFiles/parthenon.dir/mesh/mesh.cpp.o
-c
/Volumes/home/sriram/codes/parthenon/src/mesh/mesh.cpp


# hdf5 enabled
