#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <map>
#include <fstream>
#include <cstdlib>
#include <ctime>
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

struct FieldConfig {
    std::string name;
    double x_percent;
    double y_percent;
    double font_size_percent;
    std::string color;
};

struct TemplateConfig {
    std::string name;
    std::string image_path;
    std::vector<FieldConfig> fields;
};

Config load_config(const std::string& path) {
    Config cfg;
    YAML::Node config = YAML::LoadFile(path);
    YAML::Node server = config["server"];
    cfg.port = server["port"].as<int>();
    cfg.templates_path = server["templates_path"].as<std::string>();
    
    if (server["static_path"] && server["static_path"].IsScalar()) {
        cfg.static_path = server["static_path"].as<std::string>();
    } else {
        cfg.static_path = "./static";
    }
    
    if (server["font_path"] && server["font_path"].IsScalar()) {
        cfg.font_path = server["font_path"].as<std::string>();
    } else {
        cfg.font_path = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
    }
    
    return cfg;
}

TemplateConfig load_template_config(const std::string& template_name, const std::string& templates_path) {
    TemplateConfig tmpl;
    tmpl.name = template_name;
    tmpl.image_path = templates_path + "/" + template_name + ".png";
    
    std::string config_path = templates_path + "/" + template_name + ".yaml";
    if (fs::exists(config_path)) {
        YAML::Node config = YAML::LoadFile(config_path);
        if (config["fields"]) {
            for (const auto& field : config["fields"]) {
                FieldConfig fc;
                fc.name = field["name"].as<std::string>();
                fc.x_percent = field["x_percent"].as<double>();
                fc.y_percent = field["y_percent"].as<double>();
                fc.font_size_percent = field["font_size_percent"].as<double>();
                fc.color = field["color"] ? field["color"].as<std::string>() : "#000000";
                tmpl.fields.push_back(fc);
            }
        }
    } else {
        tmpl.fields = {
            {"name", 50.0, 42.0, 6.67, "#000000"},
            {"competition", 50.0, 52.0, 4.0, "#000000"},
            {"group", 50.0, 58.0, 4.0, "#000000"},
            {"place", 50.0, 64.0, 4.0, "#000000"}
        };
    }
    
    return tmpl;
}

void save_template_config(const TemplateConfig& tmpl, const std::string& templates_path) {
    std::string config_path = templates_path + "/" + tmpl.name + ".yaml";
    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "name" << YAML::Value << tmpl.name;
    out << YAML::Key << "fields" << YAML::Value;
    out << YAML::BeginSeq;
    for (const auto& field : tmpl.fields) {
        out << YAML::BeginMap;
        out << YAML::Key << "name" << YAML::Value << field.name;
        out << YAML::Key << "x_percent" << YAML::Value << field.x_percent;
        out << YAML::Key << "y_percent" << YAML::Value << field.y_percent;
        out << YAML::Key << "font_size_percent" << YAML::Value << field.font_size_percent;
        out << YAML::Key << "color" << YAML::Value << field.color;
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;
    out << YAML::EndMap;
    
    std::ofstream fout(config_path);
    fout << out.c_str();
}

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

cv::Scalar hex_to_color(const std::string& hex) {
    if (hex.length() != 7 || hex[0] != '#') return cv::Scalar(0, 0, 0);
    int r = std::stoi(hex.substr(1, 2), nullptr, 16);
    int g = std::stoi(hex.substr(3, 2), nullptr, 16);
    int b = std::stoi(hex.substr(5, 2), nullptr, 16);
    return cv::Scalar(b, g, r);
}

void render_text(cv::Mat& img, const std::string& text, int x, int y,
                 FT_Face face, double font_size, cv::Scalar color) {
    FT_Set_Pixel_Sizes(face, 0, (FT_UInt)font_size);
    auto glyphs = utf8_to_unicode(text);
    int pen_x = x;
    for (unsigned int cp : glyphs) {
        if (FT_Load_Char(face, cp, FT_LOAD_RENDER)) continue;
        FT_GlyphSlot glyph = face->glyph;
        
        for (int row = 0; row < glyph->bitmap.rows; ++row) {
            for (int col = 0; col < glyph->bitmap.width; ++col) {
                int px = pen_x + glyph->bitmap_left + col;
                int py = y - glyph->bitmap_top + row;
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

void generate_diploma(const TemplateConfig& tmpl,
                     const std::map<std::string, std::string>& data,
                     const std::string& output_path,
                     const std::string& font_path) {
    cv::Mat img = cv::imread(tmpl.image_path);
    if (img.empty()) throw std::runtime_error("Failed to load template: " + tmpl.image_path);
    
    FT_Library ft;
    if (FT_Init_FreeType(&ft)) throw std::runtime_error("Could not init FreeType");
    FT_Face face;
    if (FT_New_Face(ft, font_path.c_str(), 0, &face)) {
        FT_Done_FreeType(ft);
        throw std::runtime_error("Could not load font: " + font_path);
    }
    
    int width = img.cols;
    int height = img.rows;
    
    for (const auto& field : tmpl.fields) {
        auto it = data.find(field.name);
        if (it == data.end() || it->second.empty()) continue;
        
        double font_size = height * field.font_size_percent / 100.0;
        int x = width * field.x_percent / 100.0;
        int y = height * field.y_percent / 100.0;
        
        FT_Set_Pixel_Sizes(face, 0, (FT_UInt)font_size);
        int text_width = 0;
        for (unsigned int cp : utf8_to_unicode(it->second)) {
            if (FT_Load_Char(face, cp, FT_LOAD_DEFAULT)) continue;
            text_width += face->glyph->advance.x >> 6;
        }
        
        int centered_x = x - text_width / 2;
        cv::Scalar color = hex_to_color(field.color);
        render_text(img, it->second, centered_x, y, face, font_size, color);
    }
    
    FT_Done_Face(face);
    FT_Done_FreeType(ft);
    cv::imwrite(output_path, img);
}

int main() {
    try {
        Config cfg = load_config("config/static_config.yaml");
        httplib::Server svr;
        
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
        
        // API: получить конфигурацию шаблона
        svr.Get("/api/template/config", [&](const httplib::Request& req, httplib::Response& res) {
            std::string name = req.get_param_value("name");
            if (name.empty()) {
                res.status = 400;
                res.set_content("Missing template name", "text/plain");
                return;
            }
            
            try {
                TemplateConfig tmpl = load_template_config(name, cfg.templates_path);
                YAML::Emitter out;
                out << YAML::BeginMap;
                out << YAML::Key << "name" << YAML::Value << tmpl.name;
                out << YAML::Key << "fields" << YAML::Value;
                out << YAML::BeginSeq;
                for (const auto& field : tmpl.fields) {
                    out << YAML::BeginMap;
                    out << YAML::Key << "name" << YAML::Value << field.name;
                    out << YAML::Key << "x_percent" << YAML::Value << field.x_percent;
                    out << YAML::Key << "y_percent" << YAML::Value << field.y_percent;
                    out << YAML::Key << "font_size_percent" << YAML::Value << field.font_size_percent;
                    out << YAML::Key << "color" << YAML::Value << field.color;
                    out << YAML::EndMap;
                }
                out << YAML::EndSeq;
                out << YAML::EndMap;
                res.set_content(out.c_str(), "application/yaml");
            } catch (const std::exception& e) {
                res.status = 500;
                res.set_content(std::string("Error: ") + e.what(), "text/plain");
            }
        });
        
        // API: загрузить новый шаблон
        svr.Post("/api/template/upload", [&](const httplib::Request& req, httplib::Response& res) {
            auto it = req.files.find("file");
            if (it == req.files.end()) {
                res.status = 400;
                res.set_content("No file uploaded", "text/plain");
                return;
            }
            const auto& file = it->second;
            
            std::string filename = file.filename;
            if (filename.empty() || filename.find(".png") == std::string::npos) {
                res.status = 400;
                res.set_content("Invalid file format. Only PNG allowed", "text/plain");
                return;
            }
            
            std::string template_name = filename.substr(0, filename.length() - 4);
            std::string dest_path = cfg.templates_path + "/" + filename;
            
            std::ofstream fout(dest_path, std::ios::binary);
            fout.write(file.content.data(), file.content.size());
            fout.close();
            
            TemplateConfig tmpl;
            tmpl.name = template_name;
            tmpl.image_path = dest_path;
            tmpl.fields = {
                {"name", 50.0, 42.0, 6.67, "#000000"},
                {"competition", 50.0, 52.0, 4.0, "#000000"},
                {"group", 50.0, 58.0, 4.0, "#000000"},
                {"place", 50.0, 64.0, 4.0, "#000000"}
            };
            save_template_config(tmpl, cfg.templates_path);
            
            res.set_content("{\"success\": true, \"name\": \"" + template_name + "\"}", "application/json");
        });
        
        // API: сохранить конфигурацию шаблона
        svr.Post("/api/template/save", [&](const httplib::Request& req, httplib::Response& res) {
            try {
                YAML::Node config = YAML::Load(req.body);
                TemplateConfig tmpl;
                tmpl.name = config["name"].as<std::string>();
                tmpl.image_path = cfg.templates_path + "/" + tmpl.name + ".png";
                
                for (const auto& field : config["fields"]) {
                    FieldConfig fc;
                    fc.name = field["name"].as<std::string>();
                    fc.x_percent = field["x_percent"].as<double>();
                    fc.y_percent = field["y_percent"].as<double>();
                    fc.font_size_percent = field["font_size_percent"].as<double>();
                    fc.color = field["color"].as<std::string>();
                    tmpl.fields.push_back(fc);
                }
                
                save_template_config(tmpl, cfg.templates_path);
                res.set_content("{\"success\": true}", "application/json");
            } catch (const std::exception& e) {
                res.status = 500;
                res.set_content(std::string("Error: ") + e.what(), "text/plain");
            }
        });
        
        // API: генерация
        svr.Get("/generate", [&](const httplib::Request& req, httplib::Response& res) {
            std::string name = req.get_param_value("name");
            std::string competition = req.get_param_value("competition");
            std::string group = req.get_param_value("group");
            std::string place = req.get_param_value("place");
            std::string tmpl_name = req.get_param_value("template");
            
            if (name.empty() || competition.empty() || tmpl_name.empty()) {
                res.status = 400;
                res.set_content("Missing required parameters", "text/plain");
                return;
            }
            
            try {
                TemplateConfig tmpl = load_template_config(tmpl_name, cfg.templates_path);
                std::map<std::string, std::string> data = {
                    {"name", name},
                    {"competition", competition},
                    {"group", group},
                    {"place", place}
                };
                
                std::string output_path = "/tmp/diploma_" + std::to_string(rand()) + "_" + 
                                          std::to_string(std::time(nullptr)) + ".png";
                generate_diploma(tmpl, data, output_path, cfg.font_path);
                
                std::ifstream ifs(output_path, std::ios::binary);
                if (!ifs) {
                    res.status = 500;
                    res.set_content("Failed to read generated file", "text/plain");
                    return;
                }
                std::string body((std::istreambuf_iterator<char>(ifs)), 
                                  std::istreambuf_iterator<char>());
                ifs.close();
                
                std::remove(output_path.c_str());
                
                res.set_content(body, "image/png");
            } catch (const std::exception& e) {
                res.status = 500;
                res.set_content(std::string("Error: ") + e.what(), "text/plain");
            }
        });
        
        // API: отдача файла шаблона (PNG)
        svr.Get("/templates/:filename", [&](const httplib::Request& req, httplib::Response& res) {
            std::string filename = req.path_params.at("filename");
            std::string filepath = cfg.templates_path + "/" + filename;
            
            if (!fs::exists(filepath)) {
                res.status = 404;
                res.set_content("Template not found", "text/plain");
                return;
            }
            
            std::ifstream ifs(filepath, std::ios::binary);
            if (!ifs) {
                res.status = 500;
                res.set_content("Failed to read file", "text/plain");
                return;
            }
            std::string body((std::istreambuf_iterator<char>(ifs)), 
                              std::istreambuf_iterator<char>());
            ifs.close();
            
            res.set_content(body, "image/png");
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