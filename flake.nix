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

        ssize = pkgs.stdenv.mkDerivation {
          pname = "ssize";
          version = "0.1.0";
          src = ./.;

          nativeBuildInputs = nativeDeps;
          buildInputs = deps ++ [ pkgs.catch2_3 ];

          cmakeFlags = [
            "-DCMAKE_BUILD_TYPE=RelWithDebInfo"
            "-DSSIZE_BUILD_TESTS=ON"
          ];

          doCheck = true;
          checkPhase = ''
            ctest --output-on-failure
          '';

          meta = with pkgs.lib; {
            description = "Curated clothing catalog service";
            platforms = platforms.linux;
          };
        };
      in
      {
        packages = {
          default = ssize;
          ssize = ssize;
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
