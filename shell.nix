{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  nativeBuildInputs = [
    pkgs.pkg-config
  ];
  buildInputs = [
    pkgs.cjson
    pkgs.curl
    pkgs.gcc11
    pkgs.lua5_4
    pkgs.sqlite
  ];
}
