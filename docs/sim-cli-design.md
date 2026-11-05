# `sim`: compilar y lanzar el simulador con un solo comando

## Objetivo

Compilar, lanzar y depurar el simulador desde cualquier consola (PowerShell, CMD, la terminal de VS Code, bash o zsh) con comandos cortos:

```bash
./sim build
./sim run
./sim watch
```

Requisitos:

- **Autocontenido**: casi nada se instala aparte. Las dependencias que faltan se resuelven solas y se guardan dentro del repositorio.
- **Multiplataforma**: el mismo comando y el mismo código en Windows, macOS y Linux. No hay un script por sistema operativo.
- **Sin cambiar de entorno**: no hace falta abrir la shell CLANG64 de MSYS2 ni ajustar el `PATH` a mano.
- **Pensado para depurar**: un ciclo de editar, recompilar y relanzar rápido, con los errores de compilación visibles en la misma consola.

Este enfoque sustituye al simulador web (`web-simulator-design.md`) como herramienta de desarrollo diario. La versión web queda, si acaso, como algo opcional para demos.

---

## 1. Situación actual

| Problema | Dónde |
|---|---|
| En Windows solo se compila dentro de la shell CLANG64 de MSYS2 | `docs/windows-setup.md`, `README.md` |
| SDL2 se instala aparte con el gestor de cada sistema (`pacman`, `brew`, `apt`) | `platform/sim/CMakeLists.txt` (`find_package(SDL2 CONFIG REQUIRED)`) |
| Los lanzadores tienen rutas fijas de un usuario (`C:\Users\Skemz\...`) y hay uno por shell | `tools/run-sim.ps1`, `tools/run-sim.bat` |
| Para ejecutar el `.exe` en Windows hay que meter `C:\msys64\clang64\bin` en el `PATH` (SDL2.dll y runtimes) | `tools/run-sim.ps1` |
| Las pruebas que usan el venv solo buscan `.venv/bin/python`, así que en Windows (`.venv\Scripts\python.exe`) se omiten sin avisar | `tests/CMakeLists.txt` |
| No hay una tarjeta de música por defecto: cada uno tiene que pasar `--card` | `platform/sim/sim_options.c` |

---

## 2. Tecnologías recomendadas

| Pieza | Elección | Por qué |
|---|---|---|
| CLI `sim` | **Un único script Python 3.10+ que solo usa la biblioteca estándar**: `tools/sim.py` | El proyecto ya exige Python para las pruebas y los generadores. Si solo usa la biblioteca estándar (`argparse`, `subprocess`, `pathlib`, `json`, `shutil`), funciona antes de `setup` y es el mismo código en los tres sistemas. |
| Lanzadores | `sim` (sh) y `sim.cmd` en la raíz, de 3 líneas cada uno | Permiten escribir `./sim build` o `sim build` sin acordarse de `python tools/sim.py`. |
| CMake + Ninja | **Paquetes de pip `cmake` y `ninja` dentro de `.venv`** | Se instalan con `pip` en todas las plataformas, con versión fijada y sin tocar el sistema. |
| Compilador C | **clang del sistema, detectado automáticamente** | Es la única dependencia externa real. Windows: MSYS2 `clang64` (ya en uso). macOS: Xcode Command Line Tools. Linux: el paquete `clang`. `sim` lo localiza y ajusta el `PATH` solo para los procesos que lanza. |
| SDL2 | **`FetchContent` de CMake con la versión fijada y su SHA256, compilado estático** | No hace falta instalar SDL2 en ningún sistema. Al ser estático no hay que distribuir `SDL2.dll`. Se descarga una vez en `.cache/deps` (ya ignorado por git) y `sim clean` no lo borra. |
| Configuración local | `sim.local.json` en la raíz, ignorado por git | Guarda la carpeta de música, la escala y el directorio del sistema de cada desarrollador. Se acaban las rutas fijas en los scripts. |
| Vigilar cambios | Sondeo de fechas de modificación con la biblioteca estándar | No necesita `watchdog` ni `chokidar`. Un sondeo cada 300 ms sobre `src/` y `platform/sim/` es suficiente. |
| Depurador | `lldb` (incluido con clang en MSYS2, Xcode y LLVM) y una configuración de VS Code | Los mismos argumentos que `sim run`, pero con breakpoints. |

### Descartadas

- **make, just, Task, npm scripts**: añaden otra instalación y no resuelven nada que Python no haga ya.
- **Scripts duplicados en PowerShell y bash**: dos implementaciones que acaban divergiendo, que es justo lo que pasa hoy con `run-sim.ps1` y `run-sim.bat`.
- **Docker o contenedores**: en Windows y macOS no hay manera sencilla de tener ventana SDL ni audio dentro del contenedor.
- **vcpkg, Conan**: demasiado pesados para una sola dependencia (SDL2).
- **`zig cc` como compilador portable**: es atractivo porque es una sola descarga, pero no está claro cómo se comporta con UBSan, que se usa en el preset `dev`, ni con el recurso `.rc` de Windows (`enable_language(RC)`). Se puede evaluar más adelante para eliminar también la dependencia del compilador.
- **Simulador web**: no ayuda al ciclo de depuración diario (ver `web-simulator-design.md`).

---

## 3. Comandos

| Comando | Qué hace |
|---|---|
| `sim setup` | Crea `.venv`, instala `tools/requirements.txt` más `cmake` y `ninja`, comprueba clang y crea `build/sim/system`. Se puede volver a ejecutar sin problema. |
| `sim doctor` | Muestra qué encontró: Python, clang (ruta y versión), CMake, Ninja, SDL2 (del sistema o descargado) y la configuración local. Si falta algo, dice el comando exacto que lo arregla. |
| `sim build` | `cmake --preset dev` (solo si hace falta) y `cmake --build --preset dev --target echo_sim`. |
| `sim run [--card DIR] [-- ARGS...]` | Compila si hay cambios y lanza `echo_sim` con la tarjeta y el directorio del sistema configurados. Lo que va después de `--` se pasa tal cual (`--scale 4`, `--battery 15`, etc.). |
| `sim watch` | Compila y lanza el simulador. Cuando se guarda un `.c` o `.h`, cierra el simulador, recompila y lo vuelve a abrir. Si la compilación falla, muestra el error y espera al siguiente guardado. |
| `sim shot KEYS FILE.bmp` | Ejecuta en modo headless (`--headless --frames N --keys KEYS --screenshot FILE`). Sirve para reproducir un bug de forma determinista. |
| `sim test [FILTRO]` | `ctest --preset dev`, con `-R FILTRO` si se indica un filtro. |
| `sim debug [-- ARGS...]` | Lanza `echo_sim` bajo `lldb` con los mismos argumentos que `sim run`. |
| `sim card demo` | Genera una biblioteca sintética en `build/sim/card` con `tools/make_test_library.py --tracks 48 --covers`, que solo usa la biblioteca estándar. Es la tarjeta por defecto si no hay ninguna configurada. |
| `sim config KEY VALUE` | Escribe en `sim.local.json`, por ejemplo `sim config card D:\Music`. |
| `sim print-args` | Imprime en JSON la ruta de `echo_sim` y sus argumentos, para VS Code u otras herramientas. |
| `sim clean` | Borra `build/dev`. Mantiene `.cache/deps` y `build/sim/system`. |

Ejemplo de una sesión de depuración:

```bash
./sim setup
./sim config card ~/Music
./sim watch
```

---

## 4. Diseño de `tools/sim.py`

El script sigue las reglas del proyecto: sin comentarios (`no_comments` revisa los `.py`), funciones cortas y nombres en `snake_case`.

### Módulos internos (funciones dentro del mismo archivo)

```
sim.py
├── parse_arguments(argv)            subcomandos con argparse
├── repo_root()                      carpeta del script / ..
├── venv_python()                    .venv/bin/python o .venv/Scripts/python.exe
├── find_clang()                     ECHO_CC, PATH, C:\msys64\clang64\bin, /opt/homebrew/opt/llvm/bin
├── tool_environment()               copia de os.environ con clang, cmake y ninja al principio del PATH
├── load_config() / save_config()    sim.local.json con valores por defecto
├── configure_if_needed()            cmake --preset dev si falta build/dev/CMakeCache.txt
├── build_simulator()                cmake --build --preset dev --target echo_sim
├── simulator_path()                 build/dev/platform/sim/echo_sim(.exe)
├── simulator_arguments(config, extra)
├── run_simulator(arguments)         subprocess.Popen con tool_environment()
├── watched_files()                  src/**, platform/sim/**, *.c *.h CMakeLists.txt
├── snapshot_mtimes(paths)
├── watch_loop()                     sondeo, terminar, compilar y relanzar
└── main(argv)
```

### Reglas de comportamiento

- **El `PATH` solo se modifica para los procesos hijos.** Nunca se cambia el `PATH` del usuario ni del sistema.
- **La salida de CMake y de Ninja se muestra tal cual**, así los errores de clang salen con colores y con `archivo:línea` clicable en VS Code.
- **El código de salida de `sim` es el del proceso que lanzó**, para poder usarlo en scripts y en CI.
- **En `watch`, el simulador se cierra con `terminate()` y, si no responde, con `kill()`** tras un tiempo de espera corto. En Windows así no queda `echo_sim.exe` bloqueado mientras el enlazador intenta escribirlo.
- **Al empezar, `sim` comprueba que se ejecuta con el Python del venv.** Si no es así y el venv existe, se relanza con él.

### Lanzadores en la raíz

`sim`:

```sh
#!/bin/sh
exec python3 "$(dirname "$0")/tools/sim.py" "$@"
```

`sim.cmd`:

```bat
@echo off
python "%~dp0tools\sim.py" %*
```

Desde PowerShell, `.\sim build` encuentra `sim.cmd` automáticamente.

---

## 5. Cambios en CMake

### 5.1 SDL2 sin instalación

En `platform/sim/CMakeLists.txt`, sustituir `find_package(SDL2 CONFIG REQUIRED)` por una función en `cmake/sdl2.cmake`:

1. Si `ECHO_SDL2_BUNDLED` está en `OFF` y `find_package(SDL2 CONFIG QUIET)` lo encuentra, se usa el del sistema.
2. Si no, `FetchContent_Declare(SDL2 URL <release-2.30.x.tar.gz> URL_HASH SHA256=<hash>)` con `SDL_STATIC=ON`, `SDL_SHARED=OFF` y `SDL_TEST=OFF`, y `FETCHCONTENT_BASE_DIR` apuntando a `${PROJECT_SOURCE_DIR}/.cache/deps`.
3. Se enlaza con `SDL2::SDL2-static` y se define el alias `echo_sdl2` para que el resto de CMake no dependa de cuál de los dos se usó.

Por defecto, `ECHO_SDL2_BUNDLED=ON`, para que todo el mundo use la misma versión. Si hace falta trabajar sin red desde el primer día, se puede guardar el tarball en `third_party/SDL2/` y apuntar `URL` a esa ruta local.

### 5.2 Preset `dev` sin `-D` manuales

Añadir a `CMakePresets.json`, en el preset `dev`:

```json
"generator": "Ninja",
"cacheVariables": {
  "CMAKE_C_COMPILER": "clang",
  "CMAKE_BUILD_TYPE": "Debug",
  "ECHO_SANITIZE": "ON"
}
```

Así `cmake --preset dev` funciona igual desde `sim` que a mano, y desaparece el `cmake -S . -B build/dev -DCMAKE_GENERATOR=Ninja -DCMAKE_C_COMPILER=clang` de `windows-setup.md`.

### 5.3 El venv también en Windows

En `tests/CMakeLists.txt`, calcular la ruta del Python del venv una sola vez (`.venv/bin/python` o `.venv/Scripts/python.exe`) y usarla en todas partes. Así las pruebas que dependen del venv dejan de omitirse sin aviso en Windows.

---

## 6. Depuración

- **`sim debug`**: ejecuta `lldb -- <echo_sim> <args>`. Los argumentos salen de la misma función que usa `sim run`, así que no hay dos configuraciones que mantener.
- **VS Code (opcional)**: añadir `.vscode/launch.json` y `.vscode/tasks.json` con una tarea `sim build` y una configuración de lanzamiento con CodeLLDB que use `build/dev/platform/sim/echo_sim`. F5 compila y abre el simulador con breakpoints. Esos archivos solo contienen JSON, sin rutas personales: la carpeta de música se lee de `sim.local.json` mediante `sim print-args`.
- **Reproducir bugs**: `sim shot "down,down,play,back:long" bug.bmp` da la misma imagen siempre, porque el modo headless usa tiempo virtual. Ese comando se puede pegar en un issue.
- **UBSan** está activo en el preset `dev`. Un comportamiento indefinido detiene el simulador y muestra la línea en la misma consola.

---

## 7. Paso a paso

Cada paso deja el repositorio funcionando y pasando `ctest --preset dev`.

| # | Paso | Hecho cuando | Estimación |
|---|---|---|---|
| 1 | Generador Ninja y compilador clang en el preset `dev`; ruta del venv corregida en `tests/CMakeLists.txt` | `cmake --preset dev` funciona sin `-D`, y en Windows se ejecutan las pruebas del venv | 0,5 día |
| 2 | `cmake/sdl2.cmake` con `FetchContent` estático y caché en `.cache/deps` | Compila en una máquina sin SDL2 instalado; `echo_sim.exe` arranca sin `SDL2.dll` | 1 día |
| 3 | `tools/sim.py` con `setup`, `doctor`, `build`, `run` y `config`, más los lanzadores `sim` y `sim.cmd` | Desde PowerShell, sin abrir MSYS2: `.\sim setup` y luego `.\sim run` abren el simulador | 1 día |
| 4 | `sim card demo` como tarjeta por defecto | `sim run` en un clon recién hecho muestra una biblioteca con portadas | 0,5 día |
| 5 | `sim watch` | Guardar un `.c` cierra, recompila y reabre el simulador en pocos segundos; si hay un error de compilación, se ve y el comando espera | 1 día |
| 6 | `sim shot`, `sim test`, `sim debug` y `sim clean` | Cada comando funciona en Windows y en macOS o Linux | 0,5 día |
| 7 | Pruebas de `sim.py` en `tools/tests/test_sim.py` (argumentos, rutas del venv, configuración, construcción de argumentos), sin lanzar el simulador | Pasan dentro de `ctest --preset dev` | 0,5 día |
| 8 | Documentación: reescribir `docs/windows-setup.md` y la sección de compilación del `README.md` en torno a `sim`; borrar `tools/run-sim.ps1` y `tools/run-sim.bat` | La guía de Windows cabe en una pantalla | 0,5 día |
| | **Total** | | **unos 5–6 días** |

Los pasos 1 y 2 son independientes de `sim` y ya mejoran la compilación manual. Si solo se hace un paso, que sea el 3: es el que elimina tener que entrar en MSYS2.

---

## 8. Lo único que queda fuera del repositorio

| Sistema | Instalación única |
|---|---|
| Windows | Python 3 y MSYS2 con `mingw-w64-clang-x86_64-clang` y `mingw-w64-clang-x86_64-compiler-rt`, pero nunca hay que abrir su shell |
| macOS | Python 3 y `xcode-select --install` |
| Linux | `python3`, `python3-venv`, `clang` y las cabeceras de desarrollo de X11/Wayland y ALSA/PulseAudio que pide SDL2 al compilarse |

Todo lo demás (CMake, Ninja, SDL2, las dependencias de Python y la tarjeta de prueba) lo instala o lo genera `sim setup` dentro del repositorio.
