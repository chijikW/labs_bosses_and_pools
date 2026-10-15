# labs_bosses_and_pools

Ivan's laboratory assignments are here!!!

Этот репозиторий основан на стартовом коде
[Vulkan Starter App](https://github.com/vladeemerr/vulkan-starter-app).
Лабораторные работы выполняются на C++20 с использованием Vulkan, GLFW и ImGui.

---

# 🌋 Vulkan Starter App

## С чего начать

Перед сборкой проекта вам нужно установить компилятор C++, Vulkan SDK и CMake.

Проект использует стандарт C++20, поэтому требуется один из следующих компиляторов:
- GCC 10.X
- Clang 10
- Microsoft Visual Studio 2019

Официально проект протестирован на *Windows* и *GNU/Linux*, поддержки *macOS* пока нет.
Если у вас есть рабочее решение для macOS — отправьте PR, чтобы другие тоже
могли собрать этот пример без лишних сложностей!

<ins>**1. Скачивание репозитория**</ins>

Начните с клонирования репозитория командой
`git clone --depth 1 https://github.com/vladeemerr/vulkan-starter-app`

В этом репозитории нет подмодулей — вместо них используется механизм CMake `FetchContent`.

<ins>**2. Настройка проекта**</ins>

Выполните одну из команд CMake, чтобы скачать зависимости и настроить проект:

```bash
cmake --preset debug       # для GNU/Linux (GCC/Clang)
cmake --preset msvc-debug  # для Windows (Visual Studio 2019)
cmake --preset mingw-debug # для Windows (MinGW)