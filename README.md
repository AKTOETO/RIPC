# Разработка прототипа механизма межпроцессного взаимодействия для Linux операционных на базе unix сокетов, предоставляющего разработчикам RESTful интерфейс взаимодействия

Краткое описание проекта есть в [файле](./docs/ipc.pdf)

# Источники:
- [Про IPC Binder в Android](https://medium.com/android-news/android-binder-framework-8a28fb38699a)

# Зависимости
- [Boost.asio](https://www.boost.org/doc/libs/master/doc/html/boost_asio.html)
- [nlohmann](https://github.com/nlohmann/json) (json)

# Примечания
- Использовать [`Boost.asio`](https://www.boost.org/doc/libs/develop/doc/html/boost_asio/overview/posix/local.html) заместо обычных unix сокетов. Так проще будет прописать взаимодействие сервера и клиента.
