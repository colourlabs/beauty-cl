#pragma once

#include <string>
#include <unordered_map>

namespace beauty::header {
struct content_type {
  explicit content_type(const char *v) : value(v) {}
  std::string value;

  std::string operator()(const std::string &) const { return value; }
};
} // namespace beauty::header

namespace beauty::content_type {
inline static const beauty::header::content_type application_json{
    "application/json"};
inline static const beauty::header::content_type application_pdf{
    "application/pdf"};
inline static const beauty::header::content_type image_gif{"image/gif"};
inline static const beauty::header::content_type image_jpeg{"image/jpeg"};
inline static const beauty::header::content_type image_png{"image/png"};
inline static const beauty::header::content_type image_x_icon{"image/x-icon"};
inline static const beauty::header::content_type text_css{"text/css"};
inline static const beauty::header::content_type text_html{"text/html"};
inline static const beauty::header::content_type text_javascript{
    "text/javascript"};
inline static const beauty::header::content_type text_plain{"text/plain"};
inline static const beauty::header::content_type unknown{
    "application/octet-stream"};

const std::unordered_map<std::string, beauty::header::content_type> types = {
    {".css", text_css},          {".gif", image_gif},
    {".htm", text_html},         {".html", text_html},
    {".ico", image_x_icon},      {".jpeg", image_jpeg},
    {".jpg", image_jpeg},        {".js", text_javascript},
    {".json", application_json}, {".pdf", application_pdf},
    {".png", image_png},         {".txt", text_plain},
};
} // namespace beauty::content_type
