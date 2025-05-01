#include "ripc/ripc.hpp" // Включаем ТОЛЬКО ПУБЛИЧНЫЙ заголовок библиотеки
#include <iostream>
#include <vector>
#include <memory> // Для хранения указателей (хотя и не владеющих)
#include <string>
#include <limits>    // numeric_limits
#include <sstream>   // istringstream
#include <cstdlib>   // strtol, getenv
#include <cstring>   // strcmp
#include <cctype>    // isspace
#include <stdexcept> // Для обработки исключений из библиотеки

// Храним НЕВЛАДЕЮЩИЕ указатели, полученные от фабрики
// Используем вектор для доступа по индексу в командах тестового приложения
// Важно: Размер векторов должен быть достаточным, MAX_TEST_INSTANCES
//       задает лимит для этих векторов, а не для библиотеки.
//       Лимиты библиотеки устанавливаются через setGlobal*Limit.
std::vector<ripc::Client *> g_clients_ptr;
std::vector<ripc::Server *> g_servers_ptr;
const int MAX_TEST_INSTANCES = 8; // Лимит на количество экземпляров, отслеживаемых ТЕСТОМ
#define MAX_INPUT_LEN 256

// --- Вспомогательные функции тестового приложения ---

// Парсинг индекса [0..current_max-1] из строки
bool parse_user_index(const std::string &s, int &index, size_t current_max)
{
    try
    {
        size_t pos;
        long lval = std::stol(s, &pos);
        // Проверяем, что вся строка разобрана и индекс в допустимых границах *текущего* вектора указателей
        if (pos != s.length() || lval < 0 || static_cast<size_t>(lval) >= current_max)
        {
            std::cerr << "Error: Invalid index '" << s << "'. Must be 0-" << (current_max > 0 ? current_max - 1 : 0) << ".\n";
            return false;
        }
        index = static_cast<int>(lval);
        return true;
    }
    catch (const std::invalid_argument &)
    {
        std::cerr << "Error: Invalid number format for index: '" << s << "'\n";
        return false;
    }
    catch (const std::out_of_range &)
    {
        std::cerr << "Error: Index value out of range: '" << s << "'\n";
        return false;
    }
}

// Парсинг ID ядра (client_id, server_id, shm_id)
bool parse_id(const std::string &s, int &id)
{
    try
    {
        size_t pos;
        long lval = std::stol(s, &pos);
        // Проверка разбора всей строки + валидность ID через IS_ID_VALID
        // IS_ID_VALID доступен через ripc_types.hpp -> ripc.h
        if (pos != s.length() || !IS_ID_VALID(lval))
        {
            std::cerr << "Error: Invalid or out-of-range ID '" << s << "'. Must be 0-" << MAX_ID_VALUE << ".\n";
            return false;
        }
        id = static_cast<int>(lval);
        return true;
    }
    catch (const std::invalid_argument &)
    {
        std::cerr << "Error: Invalid number format for ID: '" << s << "'\n";
        return false;
    }
    catch (const std::out_of_range &)
    {
        std::cerr << "Error: ID value out of range: '" << s << "'\n";
        return false;
    }
}

// Парсинг смещения/длины (size_t для C++)
bool parse_size_t(const std::string &s, size_t &value)
{
    try
    {
        size_t pos;
        // Используем stoull для unsigned long long, затем кастуем к size_t
        unsigned long long ullval = std::stoull(s, &pos);
        if (pos != s.length())
        {
            std::cerr << "Error: Invalid number format '" << s << "'. Extra characters found.\n";
            return false;
        }
        // Проверка на переполнение size_t (хотя stoull может и сам бросить out_of_range)
        if (ullval > std::numeric_limits<size_t>::max())
        {
            std::cerr << "Error: Value out of range for size_t: '" << s << "'\n";
            return false;
        }
        value = static_cast<size_t>(ullval);
        // Проверка на отрицательность не нужна для unsigned
        return true;
    }
    catch (const std::invalid_argument &)
    {
        std::cerr << "Error: Invalid number format for size/offset: '" << s << "'\n";
        return false;
    }
    catch (const std::out_of_range &)
    {
        std::cerr << "Error: Value out of range for size/offset: '" << s << "'\n";
        return false;
    }
}

// --- Функции обработки команд ---

void print_commands()
{
    std::cout << "\nAvailable commands (use instance index [0..N-1]):\n";
    std::cout << "  help             - Show this help\n";
    std::cout << "  exit             - Clean up and exit\n";
    std::cout << " Client Commands:\n";
    std::cout << "  client create                      - Create & register a new client\n";
    std::cout << "  client <idx> connect <server_name> - Connect client [idx] to server\n";
    std::cout << "  client <idx> mmap                    - Map client [idx]'s shared memory\n";
    std::cout << "  client <idx> show                    - Show status of client [idx]\n";
    std::cout << "  client <idx> write <offset> <text>   - Write text from client [idx]\n";
    std::cout << "  client <idx> read <offset> <len>     - Read data for client [idx]\n";
    std::cout << "  client <idx> delete                  - Delete client instance [idx]\n";
    std::cout << " Server Commands:\n";
    std::cout << "  server create <name>                 - Create & register a new server\n";
    std::cout << "  server <idx> mmap <shm_id>           - Map sub-memory [shm_id] for server [idx]\n";
    std::cout << "  server <idx> show                    - Show status of server [idx]\n";
    std::cout << "  server <idx> write <cli_id> <off> <txt> - Write text from server [idx] to client [cli_id]\n";
    std::cout << "  server <idx> read <shm_id> <off> <len> - Read data for server [idx] from [shm_id]\n";
    std::cout << "  server <idx> delete                  - Delete server instance [idx]\n";
    std::cout << " Notes:\n";
    std::cout << " - <idx> refers to the index in this app's list (0 to " << MAX_TEST_INSTANCES - 1 << ").\n";
    std::cout << " - <cli_id>, <srv_id>, <shm_id> are kernel-assigned IDs (0 to " << MAX_ID_VALUE << ").\n";
    std::cout << " - Instances are owned and managed by the RIPC library.\n";
    std::cout << " - Deleting an instance removes it from the library and invalidates the index here.\n";
}

// Находит первый пустой слот (nullptr) или добавляет в конец, если есть место
template <typename T>
int find_free_slot_or_append(std::vector<T *> &vec, int max_slots)
{
    std::cout<<"inside find_free_slot_or_append\n";
    for (size_t i = 0; i < vec.size(); ++i)
    {
        if (vec[i] == nullptr)
        {
            return static_cast<int>(i);
        }
    }
    // Если дошли сюда, свободных нет, проверяем лимит
    if (static_cast<int>(vec.size()) < max_slots)
    {
        vec.push_back(nullptr); // Добавляем пустое место
        return static_cast<int>(vec.size() - 1);
    }
    return -1; // Превышен лимит вектора теста
}

// Обработка команды пользователя
void process_command(const std::vector<std::string> &args)
{
    if (args.empty())
        return;
    const std::string &command = args[0];
    int index = -1;
    int id_arg = -1;
    size_t offset = 0; // Используем size_t
    size_t length = 0; // Используем size_t

    try
    { // Ловим исключения из библиотеки ripc
        if (command == "client")
        {
            if (args.size() < 2)
            {
                std::cerr << "Usage: client <create | idx> ...\n";
                return;
            }
            const std::string &subcmd = args[1];

            if (subcmd == "create")
            {
                int slot = find_free_slot_or_append(g_clients_ptr, MAX_TEST_INSTANCES);
                if (slot == -1)
                {
                    std::cerr << "Error: Test app instance limit reached for clients.\n";
                    return;
                }
                // Вызов фабрики из API
                ripc::Client *new_client = ripc::createClient(); // Может бросить исключение
                g_clients_ptr[slot] = new_client;                // Сохраняем невладеющий указатель
                std::cout << "Client instance created at index " << slot << " (Kernel ID: " << new_client->getId() << ").\n";
            }
            else if (parse_user_index(args[1], index, g_clients_ptr.size()))
            {
                if (index < 0 || static_cast<size_t>(index) >= g_clients_ptr.size() || !g_clients_ptr[index])
                {
                    std::cerr << "Error: Invalid or deleted client instance index: " << index << "\n";
                    return;
                }
                ripc::Client *client = g_clients_ptr[index]; // Указатель на объект, управляемый библиотекой

                if (args.size() < 3)
                {
                    std::cerr << "Usage: client <idx> <connect|mmap|show|write|read|delete> ...\n";
                    return;
                }
                const std::string &action = args[2];

                if (action == "connect")
                {
                    if (args.size() < 4)
                    {
                        std::cerr << "Usage: client <idx> connect <server_name>\n";
                        return;
                    }
                    client->connect(args[3]); // Вызываем метод клиента
                }
                else if (action == "mmap")
                {
                    client->mmap();
                }
                else if (action == "show")
                {
                    std::cout << "--- Client Instance [" << index << "] Info ---\n";
                    std::cout << client->getInfo(); // Вызываем метод клиента
                    std::cout << "-------------------------------\n";
                }
                else if (action == "write")
                {
                    if (args.size() < 5)
                    {
                        std::cerr << "Usage: client <idx> write <offset> <text...>\n";
                        return;
                    }
                    if (!parse_size_t(args[3], offset))
                        return;
                    // Собираем текст из оставшихся аргументов
                    std::string text_to_write;
                    for (size_t i = 4; i < args.size(); ++i)
                    {
                        if (i > 4)
                            text_to_write += " ";
                        text_to_write += args[i];
                    }
                    if (text_to_write.empty())
                    {
                        std::cerr << "Error: Text cannot be empty.\n";
                        return;
                    }
                    client->write(offset, text_to_write);
                }
                else if (action == "read")
                {
                    if (args.size() < 5)
                    {
                        std::cerr << "Usage: client <idx> read <offset> <length>\n";
                        return;
                    }
                    if (!parse_size_t(args[3], offset))
                        return;
                    if (!parse_size_t(args[4], length) || length == 0)
                    {
                        std::cerr << "Error: Length must be a positive integer.\n";
                        return;
                    }
                    std::vector<char> data = client->read(offset, length);
                    std::cout << "Client [" << index << "] ID " << client->getId() << " read " << data.size() << " bytes from offset " << offset << ": \"";
                    // Печатаем как строку, заменяя непечатаемые символы
                    std::string s(data.begin(), data.end());
                    for (char c : s)
                    {
                        std::cout << (isprint(static_cast<unsigned char>(c)) ? c : '.');
                    }
                    std::cout << "\"" << std::endl;
                }
                else if (action == "delete")
                {
                    int client_id = client->getId(); // Получаем ID до удаления
                    std::cout << "Requesting deletion of client instance " << index << " (ID: " << client_id << ")\n";
                    // Вызываем функцию удаления библиотеки
                    if (ripc::deleteClient(client_id))
                    {
                        g_clients_ptr[index] = nullptr; // Убираем наш невладеющий указатель
                        std::cout << "Client instance deleted from library and test app.\n";
                    }
                    else
                    {
                        std::cerr << "Info: Client ID " << client_id << " not found or already deleted in library.\n";
                        if (g_clients_ptr[index])
                            g_clients_ptr[index] = nullptr; // Синхронизируем наш вектор
                    }
                }
                else
                {
                    std::cerr << "Unknown client action: '" << action << "'\n";
                }
            } // else: parse_user_index вывел ошибку
        }
        else if (command == "server")
        {
            if (args.size() < 2)
            {
                std::cerr << "Usage: server <create | idx> ...\n";
                return;
            }
            const std::string &subcmd = args[1];

            if (subcmd == "create")
            {
                if (args.size() < 3)
                {
                    std::cerr << "Usage: server create <name>\n";
                    return;
                }
                int slot = find_free_slot_or_append(g_servers_ptr, MAX_TEST_INSTANCES);
                if (slot == -1)
                {
                    std::cerr << "Error: Test app instance limit reached for servers.\n";
                    return;
                }
                // Вызов фабрики из API
                ripc::Server *new_server = ripc::createServer(args[2]); // Может бросить исключение
                g_servers_ptr[slot] = new_server;
                std::cout << "Server instance created at index " << slot << " as '" << args[2] << "' (Kernel ID: " << new_server->getId() << ").\n";
            }
            else if (parse_user_index(args[1], index, g_servers_ptr.size()))
            {
                if (index < 0 || static_cast<size_t>(index) >= g_servers_ptr.size() || !g_servers_ptr[index])
                {
                    std::cerr << "Error: Invalid or deleted server instance index: " << index << "\n";
                    return;
                }
                ripc::Server *server = g_servers_ptr[index]; // Указатель на объект библиотеки

                if (args.size() < 3)
                {
                    std::cerr << "Usage: server <idx> <mmap|show|write|read|delete> ...\n";
                    return;
                }
                const std::string &action = args[2];

                if (action == "mmap")
                {
                    if (args.size() < 4)
                    {
                        std::cerr << "Usage: server <idx> mmap <shm_id>\n";
                        return;
                    }
                    if (!parse_id(args[3], id_arg))
                        return; // shm_id
                    server->mmapSubmemory(id_arg);
                }
                else if (action == "show")
                {
                    std::cout << "--- Server Instance [" << index << "] Info ---\n";
                    std::cout << server->getInfo();
                    std::cout << "-------------------------------\n";
                }
                else if (action == "write")
                {
                    if (args.size() < 6)
                    {
                        std::cerr << "Usage: server <idx> write <client_id> <offset> <text...>\n";
                        return;
                    }
                    if (!parse_id(args[2], id_arg))
                        return; // client_id
                    if (!parse_size_t(args[3], offset))
                        return; // offset
                    // Собираем текст
                    std::string text_to_write;
                    for (size_t i = 4; i < args.size(); ++i)
                    { // Начинаем с 4-го аргумента (индекс 3)
                        if (i > 4)
                            text_to_write += " ";
                        text_to_write += args[i];
                    }
                    if (text_to_write.empty())
                    {
                        std::cerr << "Error: Text cannot be empty.\n";
                        return;
                    }
                    server->writeToClient(id_arg, offset, text_to_write);
                }
                else if (action == "read")
                {
                    if (args.size() < 6)
                    {
                        std::cerr << "Usage: server <idx> read <shm_id> <offset> <length>\n";
                        return;
                    }
                    if (!parse_id(args[2], id_arg))
                        return; // shm_id
                    if (!parse_size_t(args[3], offset))
                        return; // offset
                    if (!parse_size_t(args[4], length) || length == 0)
                    { // length
                        std::cerr << "Error: Length must be a positive integer.\n";
                        return;
                    }
                    std::vector<char> data = server->readFromSubmemory(id_arg, offset, length);
                    std::cout << "Server [" << index << "] ID " << server->getId() << " read " << data.size() << " bytes from shm_id " << id_arg << " at offset " << offset << ": \"";
                    std::string s(data.begin(), data.end());
                    for (char c : s)
                    {
                        std::cout << (isprint(static_cast<unsigned char>(c)) ? c : '.');
                    }
                    std::cout << "\"" << std::endl;
                }
                else if (action == "delete")
                {
                    int server_id = server->getId();
                    std::cout << "Requesting deletion of server instance " << index << " (ID: " << server_id << ")\n";
                    if (ripc::deleteServer(server_id))
                    {                                   // Вызов API библиотеки
                        g_servers_ptr[index] = nullptr; // Убираем наш указатель
                        std::cout << "Server instance deleted from library and test app.\n";
                    }
                    else
                    {
                        std::cerr << "Info: Server ID " << server_id << " not found or already deleted in library.\n";
                        if (g_servers_ptr[index])
                            g_servers_ptr[index] = nullptr;
                    }
                }
                else
                {
                    std::cerr << "Unknown server action: '" << action << "'\n";
                }
            } // else: parse_user_index вывел ошибку
        }
        else if (command == "help")
        {
            print_commands();
        }
        else if (command != "exit")
        { // exit обрабатывается в main
            std::cerr << "Unknown command: '" << command << "'\n";
        }
    }
    catch (const std::exception &e)
    { // Ловим исключения из библиотеки ripc и std
        std::cerr << "Error: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "An unknown error occurred." << std::endl;
    }
}

// Глобальная очистка для тестового приложения
void cleanup_all()
{
    std::cout << "Initiating application cleanup..." << std::endl;
    // Очистка векторов указателей (не обязательно, т.к. shutdown удалит объекты)
    g_clients_ptr.clear();
    g_servers_ptr.clear();
    std::cout << "Test app pointers cleared." << std::endl;

    // Вызываем глобальную функцию завершения библиотеки
    // Она вызовет деструкторы всех оставшихся клиентов/серверов
    // и остановит поток слушателя.
    ripc::shutdown();
}

// --- Точка входа ---
int main()
{
    std::string line;
    char input_buffer[MAX_INPUT_LEN]; // Буфер для fgets/cin.getline

    std::cout << "RIPC C++ Test Application (Factory + Singleton + Notifications)\n";

    try
    {
        ripc::initialize(); // Используем переменную окружения или дефолт

        // Установка лимитов для библиотеки (пример)
        ripc::setGlobalServerLimit(8);  // Лимит на 8 серверов в библиотеке
        ripc::setGlobalClientLimit(8); // Лимит на 64 клиента в библиотеке
        // Лимит MAX_TEST_INSTANCES (16) ограничивает только векторы g_*_ptr в этом тесте

        // --- Опционально: Регистрация пользовательских обработчиков ---
        // ripc::registerNotificationHandler(NEW_CONNECTION, [](const notification_data& ntf){ ... });
    }
    catch (const std::exception &e)
    {
        std::cerr << "FATAL: Failed to initialize RIPC library: " << e.what() << std::endl;
        return 1;
    }

    // Инициализируем векторы указателей в тестовом приложении
    g_clients_ptr.reserve(MAX_TEST_INSTANCES);
    g_servers_ptr.reserve(MAX_TEST_INSTANCES);

    print_commands();

    // Основной цикл обработки команд
    while (true)
    {
        std::cout << "> ";
        std::cout.flush();

        // Используем cin.getline для чтения строки с пробелами
        std::cin.getline(input_buffer, sizeof(input_buffer));

        if (std::cin.eof())
        { // Проверка на Ctrl+D
            std::cout << "\nEOF detected. Exiting..." << std::endl;
            break;
        }
        if (std::cin.fail())
        { // Проверка на другие ошибки (например, строка слишком длинная)
            std::cerr << "\nInput error. Please try again." << std::endl;
            std::cin.clear(); // Сбросить флаги ошибок
            // Очистить буфер ввода до новой строки
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            continue;
        }

        // Преобразуем C-строку в std::string для удобства
        std::string line_str(input_buffer);

        // Пропускаем пустые строки
        if (line_str.empty() || line_str.find_first_not_of(" \t") == std::string::npos)
        {
            continue;
        }

        // Команда exit
        if (line_str == "exit")
        {
            break;
        }

        // Простой парсинг аргументов в вектор строк
        std::vector<std::string> args;
        std::string arg_buf;
        std::istringstream iss(line_str);
        while (iss >> arg_buf)
        {
            args.push_back(arg_buf);
        }

        // Обрабатываем команду
        if (!args.empty())
        {
            process_command(args);
        }
    }

    // Очистка ресурсов перед выходом
    cleanup_all();

    std::cout << "Application finished." << std::endl;
    return 0;
}