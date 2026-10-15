# KVMem component -- the MINIMAL CONTENT-SCORING SLICE.
#
# This is the part of the donor's ops/kvmem that answers one question: which KV pages is the
# current question ABOUT. It carries
#   * the pre-RoPE raw-K harvest (raw_k_shadow / raw_k_harvest / raw_k_block_arena),
#   * the KVMem paper Eq.9 mean-K index (mean_k_index),
#   * the Eq.10 query-conditioned, softmax-OVER-BLOCKS scorer (kvmem_retrieve),
#   * the block selector that keeps sink + recent and ranks the middle (kvmem_select),
#   * and the orchestrator that publishes the selection for the ring (kvmem_score.h, header-only).
#
# NOT PORTED ON PURPOSE: the donor's window/assembly layer (kvmem_window.h, kvmem_window_plan.h,
# kvmem_window_assembly.*) and its qw3 policy/port bridge. This slice never arms a retrieval
# window, never touches the page tables, the cache positions or the attention envelope: it only
# publishes a ranking, which the storage layer consumes in ProgramImpl::select_retrieval_pages.
# That is why the visible set of a running engine is unchanged by this component alone.
#
# WHY THE LIST IS EXPLICIT: src/ops/CMakeLists.txt includes each subdirectory's sources.cmake.
# Headers here are header-only and compile through their includers, so they are not listed.
#
# OPT-IN: the component is inert unless the ring is configured and the scorer's own switch is on
# (NINFER_TERNARY_KVMEM_SCORE / NINFER_TERNARY_KVMEM, read in exactly one place: kvmem_shadow.h
# and kvmem_score.h). With the switch off it allocates nothing and the engine is as it was.

target_sources(ninfer_ops PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/raw_k_shadow.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/raw_k_shadow_launch.cu"
  "${CMAKE_CURRENT_LIST_DIR}/raw_k_harvest.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/mean_k_index.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/mean_k_index_launch.cu"
  "${CMAKE_CURRENT_LIST_DIR}/kvmem_select.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/kvmem_retrieve.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/kvmem_retrieve_launch.cu"
)
