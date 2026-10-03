#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <yaml-cpp/yaml.h>
#include <opencv2/opencv.hpp>
#include <ft2build.h>
#include FT_FREETYPE_H
#include "httplib.h"

namespace fs = std::filesystem;

struct Config {
    int port;
    std::string templates_path;
    std::string static_path;
    std::string font_path;
};

Config load_config(const std::string& path) {
    Config cfg;
    YAML::Node config = YAML::LoadFile(path);
    YAML::Node server = config["server"];
    
    cfg.port = server["port"].as<int>();
    cfg.templates_path = server["templates_path"].as<std::string>();
    
    // Безопасное чтение с дефолтными значениями для старых версий yaml-cpp
    if (server["static_path"] && server["static_path"].IsScalar()) {
        cfg.static_path = server["static_path"].as<std::string>();
    } else {
        cfg.static_path = "./static";
        std::cerr << "Warning: static_path not found in config, using default './static'\n";
    }
    
    if (server["font_path"] && server["font_path"].IsScalar()) {
        cfg.font_path = server["font_path"].as<std::string>();
    } else {
        cfg.font_path = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
        std::cerr << "Warning: font_path not found in config, using default DejaVuSans\n";
    }
    
    return cfg;
}

// Декодирование UTF-8 в code points
std::vector<unsigned int> utf8_to_unicode(const std::string& utf8) {
    std::vector<unsigned int> result;
    size_t i = 0;
    while (i < utf8.length()) {
        unsigned int cp;
        unsigned char c = utf8[i];
        if ((c & 0x80) == 0) {
            cp = c; i += 1;
        } else if ((c & 0xE0) == 0xC0) {
            cp = ((c & 0x1F) << 6) | (utf8[i+1] & 0x3F); i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            cp = ((c & 0x0F) << 12) | ((utf8[i+1] & 0x3F) << 6) | (utf8[i+2] & 0x3F); i += 3;
        } else {
            cp = ((c & 0x07) << 18) | ((utf8[i+1] & 0x3F) << 12) | 
                 ((utf8[i+2] & 0x3F) << 6) | (utf8[i+3] & 0x3F); i += 4;
        }
        result.push_back(cp);
    }
    return result;
}

// Рендеринг текста через FreeType (поддерживает кириллицу)
void render_text(cv::Mat& img, const std::string& text, int x, int y, 
                 FT_Face face, double font_size, cv::Scalar color) {
    FT_Set_Pixel_Sizes(face, 0, (FT_UInt)font_size);
    auto glyphs = utf8_to_unicode(text);
    int pen_x = x;
    
    for (unsigned int cp : glyphs) {
        if (FT_Load_Char(face, cp, FT_LOAD_RENDER)) continue;
        FT_GlyphSlot glyph = face->glyph;
        
        // ИСПРАВЛЕНО: bitmap_top - это расстояние ОТ базовой линии ВВЕРХ
        // Поэтому нужно вычитать, а не прибавлять
        for (int row = 0; row < glyph->bitmap.rows; ++row) {
            for (int col = 0; col < glyph->bitmap.width; ++col) {
                int px = pen_x + glyph->bitmap_left + col;
                int py = y - glyph->bitmap_top + row;  // <-- ИСПРАВЛЕНО: минус вместо плюса
                
                if (px >= 0 && px < img.cols && py >= 0 && py < img.rows) {
                    unsigned char alpha = glyph->bitmap.buffer[row * glyph->bitmap.width + col];
                    if (alpha > 0) {
                        cv::Vec3b& pixel = img.at<cv::Vec3b>(py, px);
                        for (int c = 0; c < 3; ++c) {
                            pixel[c] = (unsigned char)(pixel[c] * (1 - alpha/255.0) + color[c] * (alpha/255.0));
                        }
                    }
                }
            }
        }
        pen_x += glyph->advance.x >> 6;
    }
}

void generate_diploma(const std::string& template_path,
                      const std::string& name,
                      const std::string& text,
                      const std::string& output_path,
                      const std::string& font_path) {
    cv::Mat img = cv::imread(template_path);
    if (img.empty()) throw std::runtime_error("Failed to load template: " + template_path);

    FT_Library ft;
    if (FT_Init_FreeType(&ft)) throw std::runtime_error("Could not init FreeType");
    
    FT_Face face;
    if (FT_New_Face(ft, font_path.c_str(), 0, &face)) {
        FT_Done_FreeType(ft);
        throw std::runtime_error("Could not load font: " + font_path);
    }

    int width = img.cols;
    int height = img.rows;
    
    double name_font_size = std::min(width, height) / 15.0;
    double text_font_size = name_font_size * 0.6;
    
    int name_y = height * 0.42;
    int text_y = height * 0.58;
    
    // Вычисляем ширину для центрирования
    FT_Set_Pixel_Sizes(face, 0, (FT_UInt)name_font_size);
    int name_width = 0;
    for (unsigned int cp : utf8_to_unicode(name)) {
        if (FT_Load_Char(face, cp, FT_LOAD_DEFAULT)) continue;
        name_width += face->glyph->advance.x >> 6;
    }
    
    FT_Set_Pixel_Sizes(face, 0, (FT_UInt)text_font_size);
    int text_width = 0;
    for (unsigned int cp : utf8_to_unicode(text)) {
        if (FT_Load_Char(face, cp, FT_LOAD_DEFAULT)) continue;
        text_width += face->glyph->advance.x >> 6;
    }
    
    int name_x = (width - name_width) / 2;
    int text_x = (width - text_width) / 2;
    
    render_text(img, name, name_x, name_y, face, name_font_size, cv::Scalar(0, 0, 0));
    render_text(img, text, text_x, text_y, face, text_font_size, cv::Scalar(0, 0, 0));
    
    FT_Done_Face(face);
    FT_Done_FreeType(ft);
    cv::imwrite(output_path, img);
}

int main() {
    try {
        Config cfg = load_config("config/static_config.yaml");
        httplib::Server svr;

        // Раздача статики (UI)
        svr.set_mount_point("/", cfg.static_path);

        // API: список шаблонов
        svr.Get("/api/templates", [&](const httplib::Request&, httplib::Response& res) {
            std::string json = "[";
            bool first = true;
            if (fs::exists(cfg.templates_path) && fs::is_directory(cfg.templates_path)) {
                for (const auto& entry : fs::directory_iterator(cfg.templates_path)) {
                    if (entry.is_regular_file() && entry.path().extension() == ".png") {
                        if (!first) json += ",";
                        json += "\"" + entry.path().stem().string() + "\"";
                        first = false;
                    }
                }
            }
            json += "]";
            res.set_content(json, "application/json");
        });

        // API: генерация
        svr.Get("/generate", [&](const httplib::Request& req, httplib::Response& res) {
            std::string name = req.get_param_value("name");
            std::string text = req.get_param_value("text");
            std::string tmpl = req.get_param_value("template");

            if (name.empty() || text.empty() || tmpl.empty()) {
                res.status = 400;
                res.set_content("Missing parameters: name, text, template", "text/plain");
                return;
            }

            std::string template_path = cfg.templates_path + "/" + tmpl + ".png";
            std::string output_path = "/tmp/diploma_" + std::to_string(rand()) + ".png";

            try {
                generate_diploma(template_path, name, text, output_path, cfg.font_path);
                res.set_file_content(output_path, "image/png");
            } catch (const std::exception& e) {
                res.status = 500;
                res.set_content(std::string("Error: ") + e.what(), "text/plain");
            }
        });

        svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
            res.set_content("OK", "text/plain");
        });

        std::cout << "Server starting on port " << cfg.port << "..." << std::endl;
        svr.listen("0.0.0.0", cfg.port);
    } catch (const std::exception& e) {
        std::cerr << "Fatal Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}