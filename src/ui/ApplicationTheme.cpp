#include "ui/ApplicationTheme.h"

#include <QApplication>
#include <QColor>
#include <QStyle>
#include <QStyleFactory>

namespace rtsa {
namespace {

void setNormalColors(QPalette& palette, QPalette::ColorGroup group)
{
    palette.setColor(group, QPalette::Window, QColor(20, 25, 32));
    palette.setColor(group, QPalette::WindowText, QColor(230, 237, 243));
    palette.setColor(group, QPalette::Base, QColor(13, 20, 27));
    palette.setColor(group, QPalette::AlternateBase, QColor(25, 33, 41));
    palette.setColor(group, QPalette::Text, QColor(242, 245, 247));
    palette.setColor(group, QPalette::Button, QColor(39, 50, 61));
    palette.setColor(group, QPalette::ButtonText, QColor(244, 247, 250));
    palette.setColor(group, QPalette::BrightText, QColor(255, 255, 255));
    palette.setColor(group, QPalette::Light, QColor(83, 97, 112));
    palette.setColor(group, QPalette::Midlight, QColor(61, 72, 84));
    palette.setColor(group, QPalette::Mid, QColor(46, 55, 65));
    palette.setColor(group, QPalette::Dark, QColor(22, 28, 35));
    palette.setColor(group, QPalette::Shadow, QColor(9, 12, 16));
    palette.setColor(group, QPalette::Highlight, QColor(0, 125, 197));
    palette.setColor(group, QPalette::HighlightedText, QColor(255, 255, 255));
    palette.setColor(group, QPalette::ToolTipBase, QColor(247, 249, 251));
    palette.setColor(group, QPalette::ToolTipText, QColor(17, 24, 32));
    palette.setColor(group, QPalette::Link, QColor(77, 184, 255));
    palette.setColor(group, QPalette::LinkVisited, QColor(188, 140, 255));
    palette.setColor(group, QPalette::PlaceholderText, QColor(139, 151, 162));
}

void setDisabledColors(QPalette& palette)
{
    constexpr QPalette::ColorGroup group = QPalette::Disabled;
    palette.setColor(group, QPalette::Window, QColor(20, 25, 32));
    palette.setColor(group, QPalette::WindowText, QColor(169, 179, 188));
    palette.setColor(group, QPalette::Base, QColor(39, 49, 58));
    palette.setColor(group, QPalette::AlternateBase, QColor(45, 56, 66));
    palette.setColor(group, QPalette::Text, QColor(208, 216, 223));
    palette.setColor(group, QPalette::Button, QColor(37, 45, 53));
    palette.setColor(group, QPalette::ButtonText, QColor(190, 200, 209));
    palette.setColor(group, QPalette::BrightText, QColor(238, 243, 247));
    palette.setColor(group, QPalette::Light, QColor(72, 83, 94));
    palette.setColor(group, QPalette::Midlight, QColor(58, 68, 78));
    palette.setColor(group, QPalette::Mid, QColor(44, 53, 62));
    palette.setColor(group, QPalette::Dark, QColor(22, 28, 34));
    palette.setColor(group, QPalette::Shadow, QColor(9, 12, 15));
    palette.setColor(group, QPalette::Highlight, QColor(54, 90, 112));
    palette.setColor(group, QPalette::HighlightedText, QColor(232, 238, 243));
    palette.setColor(group, QPalette::ToolTipBase, QColor(247, 249, 251));
    palette.setColor(group, QPalette::ToolTipText, QColor(17, 24, 32));
    palette.setColor(group, QPalette::Link, QColor(122, 190, 232));
    palette.setColor(group, QPalette::LinkVisited, QColor(190, 161, 226));
    palette.setColor(group, QPalette::PlaceholderText, QColor(151, 162, 172));
}

} // namespace

QPalette createApplicationPalette()
{
    QPalette palette;
    setNormalColors(palette, QPalette::Active);
    setNormalColors(palette, QPalette::Inactive);
    setDisabledColors(palette);
    return palette;
}

void applyApplicationTheme(QApplication& application)
{
    if (QStyle* fusionStyle = QStyleFactory::create(QStringLiteral("Fusion"))) {
        application.setStyle(fusionStyle);
    }
    application.setPalette(createApplicationPalette());
}

} // namespace rtsa
