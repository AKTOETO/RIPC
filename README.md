# Разработка прототипа механизма межпроцессного взаимодействия для Linux операционных на базе unix сокетов, предоставляющего разработчикам RESTful интерфейс взаимодействия

Краткое описание проекта есть в [файле](./docs/ipc.pdf)

# Источники:
- [Про IPC Binder в Android](https://medium.com/android-news/android-binder-framework-8a28fb38699a)
- [Примеры boost.asio](https://www.boost.org/doc/libs/master/doc/html/boost_asio/examples/cpp11_examples.html)

# Зависимости
- [Boost.asio](https://www.boost.org/doc/libs/master/doc/html/boost_asio.html)
# Примечания
- Использовать [`Boost.asio`](https://www.boost.org/doc/libs/develop/doc/html/boost_asio/overview/posix/local.html) заместо обычных unix сокетов. Так проще будет прописать взаимодействие сервера и клиента.
- Клиент и сервер будут работать асинхронно.

Некоторые заметки будут [тут](./docs/notes.md)

# Сборка
### **Установка с использованием скриптов**

1. Склонировать репозиторий и перейти в папку с репозиторием:
```
git clone https://github.com/AKTOETO/RIPC.git
cd RIPC/
```
2. Выдать права на исполнение трем скриптам
```
chmod +x initial-conf.sh compile-debug.sh compile-release.sh 
```
3. Запустить скрипт `initial-conf.sh` для начальной настройки репозитория (установки cmake и conan; создание профиля в conan; загрузки нужных библиотек через conan; создание папки `build` для бинарников). 
> Если запустить `initial-conf.sh` без параметров, то проект будет конфигурироваться под тип сборки `Release` (от этого будет зависеть следующий запускаемый скрипт). Если нужна конкретная сборка (всего типа два: `Debug`; `Release`), то нужно передать тип сборки в скрипт.
```
./initial-conf.sh
```
4. Запускаем скрипт `compile-release.sh`, который сконфигурирует проект через `cmake` в соответствии с текущим типом сборки (на предыдущем шаге говорилось про тип сборки) и скомпилирует все необходимые таргеты.
```
./compile-release.sh
```
> Если нужен тип сборки `Debug`, то вызываем скрипт `compile-debug.sh`
5. Получили 3 исполняемых файла:
```
./build/Release/bin/...
```
