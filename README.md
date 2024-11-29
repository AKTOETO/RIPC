# Разработка прототипа механизма межпроцессного взаимодействия для Linux операционных на базе unix сокетов, предоставляющего разработчикам RESTful интерфейс взаимодействия

Краткое описание проекта есть в [файле](./docs/ipc.pdf)

# Источники:
- [Про IPC Binder в Android](https://medium.com/android-news/android-binder-framework-8a28fb38699a)
- [Примеры boost.asio](https://www.boost.org/doc/libs/master/doc/html/boost_asio/examples/cpp11_examples.html)

# Зависимости
- [Boost.asio](https://www.boost.org/doc/libs/master/doc/html/boost_asio.html)
# Примечания
- Использовать `Boost.asio` заместо обычных unix сокетов. Так проще будет прописать взаимодействие сервера и клиента.
- Клиент и сервер будут работать асинхронно.
