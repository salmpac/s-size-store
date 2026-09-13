{
  description = "s-size — curated clothing catalog";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};

        # Runtime/compile dependencies, shared by the package and the dev shell.
        deps = with pkgs; [
          boost
          sqlite
          libsodium
          nlohmann_json
          spdlog
          tomlplusplus
          inja
        ];

        nativeDeps = with pkgs; [
          cmake
          ninja
          pkg-config
        ];

        # Builds ssize against `pkgSet`. Used twice: once with the normal
        # package set for development, once with pkgsStatic for the artifact
        # that actually ships.
        mkSsize = { pkgSet, static ? false }: pkgSet.stdenv.mkDerivation {
          pname = "ssize";
          version = "0.1.0";
          src = ./.;

          # cmake/ninja/pkg-config must be native (they run on the build host);
          # the libraries come from the target package set.
          nativeBuildInputs = nativeDeps
            ++ pkgs.lib.optionals static [ pkgs.dpkg pkgs.fakeroot ];
          buildInputs = with pkgSet; [
            boost sqlite libsodium nlohmann_json spdlog inja
            # Its test suite asserts on locales musl does not ship (ja_JP,
            # tr_TR, ...), which has nothing to do with the library itself.
            (tomlplusplus.overrideAttrs (_: { doCheck = false; }))
          ] ++ pkgSet.lib.optional (!static) pkgSet.catch2_3;

          cmakeFlags = [
            "-DCMAKE_BUILD_TYPE=RelWithDebInfo"
            "-DSSIZE_BUILD_TESTS=${if static then "OFF" else "ON"}"
          ] ++ pkgSet.lib.optionals static [
            "-DSSIZE_STATIC=ON"
            "-DCMAKE_EXE_LINKER_FLAGS=-static"
          ];

          doCheck = !static;
          checkPhase = ''
            ctest --output-on-failure
          '';

          # The static build also produces the .deb that gets deployed. Doing it
          # here rather than by hand keeps the artifact reproducible: same
          # inputs, same package.
          postInstall = pkgSet.lib.optionalString static ''
            fakeroot cpack -G DEB
            mkdir -p $out/share/ssize-deb
            cp *.deb $out/share/ssize-deb/
          '';

          meta = with pkgs.lib; {
            description = "Curated clothing catalog service";
            platforms = platforms.linux;
          };
        };
      in
      {
        packages = rec {
          default = ssize;
          ssize = mkSsize { pkgSet = pkgs; };
          # Fully static (musl) build: the binary carries every library it
          # needs, so the .deb depends on nothing and runs on any Linux the
          # server happens to be. This is what gets packaged and deployed.
          ssize-static = mkSsize { pkgSet = pkgs.pkgsStatic; static = true; };
        };

        devShells.default = pkgs.mkShell {
          packages = nativeDeps ++ deps ++ (with pkgs; [
            catch2_3
            gdb
            jq
            sqlite            # sqlite3 CLI for poking at the db
            dpkg              # CPack DEB generator
            fakeroot
            clang-tools       # clangd, clang-format
          ]);

          shellHook = ''
            echo "s-size dev shell — cmake --preset dev && cmake --build build/dev"
          '';
        };
      });
}
