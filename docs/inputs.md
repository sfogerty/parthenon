### List of input file parameters (incomplete)

   |             Option                    | Default  | Type   | Description |
   | ------------------------------------: | :------- | :----- | :---------- |
   | <parthenon/time><br>perf_cycle_offset | 0        | int    | Skip the first N cycles when calculating the final performance (e.g., zone-cycles/wall_second). Allows to hide the initialization overhead in Parthenon, which usually takes place in the first cycles when Containers are allocated, etc. | 
   | <parthenon/time><br>ncycle_out        | 1        | int    | Number of cycles between short diagnostic output to standard out containing, e.g., current time, dt, zone-update/wsec. Default: 1 (i.e, every cycle).|
   | <parthenon/time><br>ncycle_out_mesh   | 0        | int    | Number of cycles between printing the mesh structure (e.g., total number of MeshBlocks and number of MeshBlocks per level) to standard out. Use a negative number to also print every time the mesh was modified. Default: 0 (i.e, off). |
   | <parthenon/time><br>ncrecv_bdry_buf_timeout_sec | -1.0     | Real   | Timeout in seconds for the `cell_centered_bvars::ReceiveBoundaryBuffers` tasks. Disabed (negative) by default. Typically no need in production runs. Useful for debugging MPI calls. |
   | <parthenon/mesh><br>nghost            | 2        | int    | Number of ghost cells for each mesh block on each side. | 
   | <parthenon/sparse><br>enable_sparse            | `true`        | bool    | If set to false, sparse variables will always be allocated, see also [sparse doc](interface/sparse.md#run-time). |
   | <parthenon/sparse><br>alloc_threshold     | 1e-12        | float   | Global (for all sparse variables) threshold to trigger allocation of a variable if cells in the receiving ghost cells are above this value. |
   | <parthenon/sparse><br>dealloc_threshold   | 1e-14        | float   | Global (for all sparse variables) threshold to trigger deallocation if all active cells of a variable in a block are below this value. |
   | <parthenon/sparse><br>dealloc_count       | 5            | int     | First deallocate a sparse variable if the `dealloc_threshold` has been met in this number of consecutive cycles. |
