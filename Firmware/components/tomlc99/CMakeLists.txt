# Local tomlc99 component - upstream cktan/tomlc99 (submodule commit 29076dfd),
# vendored to match intechstudio/grid-fw's build.
#
# sukuwc/grid_common REQUIRES a component named `tomlc99` and its grid_config.c
# #includes "tomlc99/toml.h"; neither is bundled by the registry package.
idf_component_register(
    SRCS "tomlc99/toml.c"
    INCLUDE_DIRS "." "tomlc99"
)

# toml.c indexes arrays with plain `char` values; IDF builds with -Werror.
set_source_files_properties(
    "${CMAKE_CURRENT_LIST_DIR}/tomlc99/toml.c"
    PROPERTIES COMPILE_FLAGS "-Wno-char-subscripts -Wno-error=char-subscripts"
)
