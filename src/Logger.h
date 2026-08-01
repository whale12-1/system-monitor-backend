#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <memory>
#include <vector>

class Logger {
public:
    static void Init() {
        // 1. Логгер для консоли (с цветным выводом)
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");

        // 2. Ротируемый логгер в файл (макс. 5 МБ, максимум 3 файла)
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            "logs/system_monitor.log", 1024 * 1024 * 5, 3);
        file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] %v");

        // Объединяем вывод в один мульти-логгер
        std::vector<spdlog::sink_ptr> sinks{ console_sink, file_sink };
        auto logger = std::make_shared<spdlog::logger>("app", sinks.begin(), sinks.end());

        logger->set_level(spdlog::level::debug);
        logger->flush_on(spdlog::level::info); // Автоматический сброс на диск при INFO и выше

        spdlog::set_default_logger(logger);
    }
};

// Удобные глобальные макросы для вызова
#define LOG_TRACE(...) SPDLOG_TRACE(__VA_ARGS__)
#define LOG_DEBUG(...) SPDLOG_DEBUG(__VA_ARGS__)
#define LOG_INFO(...)  SPDLOG_INFO(__VA_ARGS__)
#define LOG_WARN(...)  SPDLOG_WARN(__VA_ARGS__)
#define LOG_ERROR(...) SPDLOG_ERROR(__VA_ARGS__)