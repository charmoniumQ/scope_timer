# save this as shell.nix
{ pkgs ? import <nixpkgs> {}}:

pkgs.mkShell {
  nativeBuildInputs = [ pkgs.bazel pkgs.clang_12 pkgs.openjdk11 ];
}
