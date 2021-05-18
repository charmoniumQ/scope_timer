{ pkgs ? import <nixpkgs> {}}:
pkgs.mkShell {
  nativeBuildInputs = [
    pkgs.bazel
    pkgs.cacert # for bazel
    pkgs.clang_12
    pkgs.clang-tools # for clang-tidy
    pkgs.git # for bazel
    pkgs.jdk11_headless # for bazel
  ];
}
