{ pkgs ? import <nixpkgs> {}}:
let 
    esp-idf-nix = builtins.fetchTarball "https://github.com/mirrexagon/nixpkgs-esp-dev/archive/master.tar.gz";
    esp-idf-pkgs = import "${esp-idf-nix}/default.nix";
in
pkgs.mkShell rec {
    name = "esp-idf-esp32s3-nix";

    buildInputs = [ esp-idf-pkgs.esp-idf-esp32s3 pkgs.pkg-config pkgs.stdenv.cc pkgs.systemdMinimal ];

    shellHook = ''
    # custom bashrc stuff
    export PS1_PREFIX="(esp-idf)"
    . ~/.bashrc
    '';
}
