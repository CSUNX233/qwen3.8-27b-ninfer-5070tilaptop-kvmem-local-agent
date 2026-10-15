# The measured device route profiles ship inside the binary: device_profiles.json (ninfer-calibrate
# --print output of each part, merged) becomes a string in the generated source.
set(NINFER_DEVICE_PROFILES_FILE ${CMAKE_CURRENT_SOURCE_DIR}/engine/device_profiles.json)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${NINFER_DEVICE_PROFILES_FILE})
file(READ ${NINFER_DEVICE_PROFILES_FILE} NINFER_DEVICE_PROFILES_JSON)
# MSVC refuses a single string literal longer than 16380 bytes (C2026) and this table is ~26 KiB, so
# it ships as a run of adjacent raw string literals the preprocessor concatenates. The payload is
# ASCII JSON that cannot contain the `)ninfer_json"` terminator, so no chunk can end the literal
# early.
string(LENGTH "${NINFER_DEVICE_PROFILES_JSON}" NINFER_DEVICE_PROFILES_LENGTH)
set(NINFER_DEVICE_PROFILES_CHUNKS "")
set(NINFER_DEVICE_PROFILES_CHUNK_BYTES 4096)
set(_profiles_offset 0)
while(_profiles_offset LESS NINFER_DEVICE_PROFILES_LENGTH)
  string(SUBSTRING "${NINFER_DEVICE_PROFILES_JSON}" ${_profiles_offset}
                   ${NINFER_DEVICE_PROFILES_CHUNK_BYTES} _profiles_chunk)
  string(APPEND NINFER_DEVICE_PROFILES_CHUNKS
                "R\"ninfer_json(${_profiles_chunk})ninfer_json\"\n    ")
  math(EXPR _profiles_offset "${_profiles_offset} + ${NINFER_DEVICE_PROFILES_CHUNK_BYTES}")
endwhile()
configure_file(engine/device_profiles_builtin.cpp.in
               ${CMAKE_CURRENT_BINARY_DIR}/engine/device_profiles_builtin.cpp @ONLY)

add_library(ninfer_runtime_support STATIC
  engine/admission_policy.cpp
  engine/context_cache/context_cost.cpp
  engine/context_cache/context_cost_defaults.cpp
  engine/device_profile.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/engine/device_profiles_builtin.cpp
  engine/kv_capacity.cpp
  engine/public_types.cpp
  prefix_cache/block_hash.cpp
  prefix_cache/prefix_index.cpp
  prefix_cache/tap_planner.cpp
)
ninfer_internal_includes(ninfer_runtime_support)
target_link_libraries(ninfer_runtime_support PUBLIC ninfer_core ninfer_ops PRIVATE ninfer::json)

# Public Engine and common contracts; model execution remains in the model layer.
add_library(ninfer_engine STATIC
  engine/model_instance.cpp
  contract/sampling.cpp
  engine/engine.cpp
)
ninfer_internal_includes(ninfer_engine)
target_include_directories(ninfer_engine PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(ninfer_engine PRIVATE
  ninfer_calibration
  ninfer_model_runtime
  ninfer_artifact
  ninfer_core
  ninfer_runtime_support
  ninfer_ops
  ninfer_text
  ninfer_media_decode)
add_library(ninfer::engine ALIAS ninfer_engine)
