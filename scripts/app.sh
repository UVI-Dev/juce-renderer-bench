# Sourced by the other scripts: the path of a built app's executable.
R=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
app() { echo "$R/build/$1_artefacts/Release/$1.app/Contents/MacOS/$1"; }
