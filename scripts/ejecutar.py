#!/usr/bin/env python3
"""Compila (si hace falta) y ejecuta `referee_scheduler` con una instancia JSON.

Uso:
    python3 scripts/ejecutar.py data/instances/ejemplo.json
    python3 scripts/ejecutar.py instancia.json --salida resultado.json
    python3 scripts/ejecutar.py instancia.json --max-nodos 500000

El primer argumento es siempre el fichero de instancia. Los demás argumentos se
pasan tal cual al binario. El script compila antes de ejecutar si el binario no
existe o si algún fuente es más reciente; usa CMake si está instalado y, si no,
cae a `g++` (con libcurl si `pkg-config` la encuentra).

Opciones propias del script (no se pasan al binario):
    --recompilar    Fuerza la compilación aunque el binario esté actualizado.
    --sin-compilar  No compila; falla si el binario no existe.
    --sin-json      No genera el JSON por defecto en data/generated/.
    --ayuda, -h     Muestra esta ayuda.

Por defecto, si no se pasa --salida, el informe JSON completo se guarda en
data/generated/resultado_<nombre-del-input>.json. Así con solo el fichero de
entrada se obtiene el resumen de terminal y el detalle por árbitro.
"""

from __future__ import annotations

import os
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
DIRECTORIO_BUILD = RAIZ / "build"
BINARIO = DIRECTORIO_BUILD / "referee_scheduler"


def escribir_ayuda() -> None:
    print(__doc__.strip())


def fuentes_mas_nuevas_que(binario: Path) -> bool:
    """¿Hay algún fuente o cabecera posterior al binario?"""
    if not binario.exists():
        return True

    momento_binario = binario.stat().st_mtime
    patrones = ("src/**/*.cpp", "include/**/*.hpp", "CMakeLists.txt")
    for patron in patrones:
        for ruta in RAIZ.glob(patron):
            if ruta.is_file() and ruta.stat().st_mtime > momento_binario:
                return True
    return False


def compilar_con_cmake() -> bool:
    cmake = shutil.which("cmake")
    if cmake is None:
        return False

    print("Compilando con CMake...", file=sys.stderr)
    try:
        subprocess.run(
            [cmake, "-S", str(RAIZ), "-B", str(DIRECTORIO_BUILD),
             "-DCMAKE_BUILD_TYPE=Release"],
            check=True,
            stdout=sys.stderr,
            stderr=sys.stderr,
        )
        subprocess.run(
            [cmake, "--build", str(DIRECTORIO_BUILD),
             "--target", "referee_scheduler",
             "-j", str(os.cpu_count() or 2)],
            check=True,
            stdout=sys.stderr,
            stderr=sys.stderr,
        )
        return True
    except subprocess.CalledProcessError:
        print("CMake falló; se intenta compilar directamente con g++.",
              file=sys.stderr)
        return False


def compilar_con_gpp() -> None:
    gpp = shutil.which("g++")
    if gpp is None:
        raise SystemExit(
            "Error: no se encontró ni CMake ni g++; no se puede compilar."
        )

    DIRECTORIO_BUILD.mkdir(parents=True, exist_ok=True)

    fuentes = [RAIZ / "src" / "main.cpp"]
    fuentes.extend(sorted((RAIZ / "src" / "algorithms").glob("*.cpp")))

    # El cliente OSRM solo se compila si libcurl está disponible. Sin ella el
    # binario arranca pero avisa de que no puede resolver instancias.
    cflags: list[str] = []
    libs: list[str] = []
    con_curl = False
    pkg_config = shutil.which("pkg-config")
    if pkg_config is not None:
        prueba = subprocess.run(
            [pkg_config, "--exists", "libcurl"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        if prueba.returncode == 0:
            con_curl = True
            cflags = shlex.split(
                subprocess.check_output(
                    [pkg_config, "--cflags", "libcurl"], text=True
                ).strip()
            )
            libs = shlex.split(
                subprocess.check_output(
                    [pkg_config, "--libs", "libcurl"], text=True
                ).strip()
            )

    fuentes_io = sorted((RAIZ / "src" / "io").glob("*.cpp"))
    if not con_curl:
        fuentes_io = [ruta for ruta in fuentes_io if ruta.name != "osrm.cpp"]

    comando = [
        gpp,
        "-std=c++20",
        "-O2",
        "-Wall",
        "-Wextra",
        "-Wpedantic",
        "-I",
        str(RAIZ / "include"),
        *cflags,
    ]
    if con_curl:
        comando.append("-DREFEREE_SCHEDULER_CON_CURL=1")
    comando.extend(str(ruta) for ruta in fuentes)
    comando.extend(str(ruta) for ruta in fuentes_io)
    comando.extend(["-o", str(BINARIO), *libs])

    print("Compilando con g++...", file=sys.stderr)
    subprocess.run(comando, check=True, stdout=sys.stderr, stderr=sys.stderr)


def compilar() -> None:
    if compilar_con_cmake():
        return
    compilar_con_gpp()


def main(argv: list[str]) -> int:
    if not argv or argv[0] in ("-h", "--ayuda"):
        escribir_ayuda()
        return 0

    if argv[0].startswith("-"):
        print("Error: el primer argumento debe ser el fichero de instancia.",
              file=sys.stderr)
        escribir_ayuda()
        return 1

    entrada = Path(argv[0]).expanduser().resolve()
    extras = argv[1:]

    recompilar = "--recompilar" in extras
    sin_compilar = "--sin-compilar" in extras
    sin_json = "--sin-json" in extras
    extras = [arg for arg in extras
              if arg not in ("--recompilar", "--sin-compilar", "--sin-json")]

    if not entrada.is_file():
        print(f"Error: no existe el fichero de instancia: {entrada}",
              file=sys.stderr)
        return 1

    if recompilar:
        try:
            compilar()
        except subprocess.CalledProcessError as error_compilacion:
            print(f"Error: la compilación falló (código "
                  f"{error_compilacion.returncode}).", file=sys.stderr)
            return 1
    elif not BINARIO.exists():
        if sin_compilar:
            print(f"Error: el binario no existe y se pidió --sin-compilar: "
                  f"{BINARIO}", file=sys.stderr)
            return 1
        try:
            compilar()
        except subprocess.CalledProcessError as error_compilacion:
            print(f"Error: la compilación falló (código "
                  f"{error_compilacion.returncode}).", file=sys.stderr)
            return 1
    elif fuentes_mas_nuevas_que(BINARIO):
        if sin_compilar:
            print("Aviso: hay fuentes más recientes que el binario, pero se "
                  "pidió --sin-compilar.", file=sys.stderr)
        else:
            try:
                compilar()
            except subprocess.CalledProcessError as error_compilacion:
                print(f"Error: la compilación falló (código "
                      f"{error_compilacion.returncode}).", file=sys.stderr)
                return 1

    if not BINARIO.exists():
        print(f"Error: no se generó el binario esperado: {BINARIO}",
              file=sys.stderr)
        return 1

    # Si el usuario no pidió una salida concreta, se guarda el informe completo
    # en data/generated/. La terminal sigue mostrando solo el resumen.
    if not sin_json and "--salida" not in extras:
        directorio_salida = RAIZ / "data" / "generated"
        directorio_salida.mkdir(parents=True, exist_ok=True)
        ruta_json = directorio_salida / f"resultado_{entrada.stem}.json"
        extras = ["--salida", str(ruta_json), *extras]

    try:
        resultado = subprocess.run(
            [str(BINARIO), str(entrada), *extras],
            cwd=RAIZ,
        )
    except KeyboardInterrupt:
        return 130

    return resultado.returncode


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
