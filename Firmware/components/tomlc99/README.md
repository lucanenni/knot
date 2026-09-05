# tomlc99 (vendored)

Plain-C TOML parser from <https://github.com/cktan/tomlc99> (MIT, see
`tomlc99/LICENSE`).

`sukuwc/grid_common` as published on the ESP component registry declares
`REQUIRES tomlc99` in its `CMakeLists.txt` but does **not** bundle or declare
`tomlc99` as a managed dependency, so a plain `idf.py build` of this repo fails
to resolve it. Upstream's Docker builder must provide it out-of-band.

This local component satisfies that requirement. `grid_config.c` includes it as
`tomlc99/toml.h`; `lua_source_collection.h` as `toml.h` — hence the two
`INCLUDE_DIRS`.

Update: re-copy `toml.c` / `toml.h` from a tagged release of cktan/tomlc99.
