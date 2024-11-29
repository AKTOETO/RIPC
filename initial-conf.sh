#!/bin/bash

# Установка зависимостей (если еще не установлены)
pip install conan cmake

# Обнаружение профиля
conan profile detect --force

# Получение типа сборки из аргументов командной строки
build_type="${1:-Release}"  # По умолчанию Release, если аргумент не передан

# Проверка допустимости типа сборки
if [[ "$build_type" != "Debug" && "$build_type" != "Release" ]]; then
  echo "Некорректный тип сборки. Допустимые значения: Debug, Release"
  exit 1
fi

# Создание директории для сборки
mkdir -p build

# Установка зависимостей и сборка проекта
conan install . --settings=build_type=${build_type} --build=missing