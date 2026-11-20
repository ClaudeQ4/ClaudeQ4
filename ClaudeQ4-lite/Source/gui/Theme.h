/*
    Claude-Q 4 - visual theme: colours, fonts (Inter, OFL), vector icons, look and feel
*/
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "dsp/FilterDesign.h"

namespace cq
{
    namespace theme
    {
        // surfaces
        const juce::Colour bgTop        { 0xff171922 };
        const juce::Colour bgBottom     { 0xff0b0c11 };
        const juce::Colour barBg        { 0xff101117 };
        const juce::Colour barLine      { 0xff1d1f29 };
        const juce::Colour panelBg      { 0xf2191b24 };
        const juce::Colour panelEdge    { 0xff2c2f3d };
        const juce::Colour panelInner   { 0xff20222d };
        const juce::Colour controlBg    { 0xff252836 };
        const juce::Colour controlHover { 0xff2e3243 };

        // text
        const juce::Colour text         { 0xffd6d8e1 };
        const juce::Colour textDim      { 0xff8b8fa0 };
        const juce::Colour textFaint    { 0xff5d6172 };

        // signal colours
        const juce::Colour accent       { 0xfff2a94a };   // composite curve / highlights (amber)
        const juce::Colour accentSoft   { 0xffc9873a };
        const juce::Colour gridLine     { 0x0fffffff };
        const juce::Colour gridMajor    { 0x1cffffff };
        const juce::Colour zeroLine     { 0x30ffffff };
        const juce::Colour gainLabel    { 0xffd9a24b };
        const juce::Colour analyzerPre  { 0xff7d8296 };
        const juce::Colour analyzerPost { 0xffc9ccd8 };
        const juce::Colour analyzerSc   { 0xffe0705a };
        const juce::Colour collision    { 0xffff4d5e };
        const juce::Colour meterGreen   { 0xff7fd36b };
        const juce::Colour meterYellow  { 0xffe7d35a };
        const juce::Colour meterRed     { 0xffff5b52 };
        const juce::Colour learn        { 0xff4fb3ff };

        juce::Colour bandColour (int band);
        juce::Colour placementColour (Placement p);

        // fonts (Inter).  The typefaces live in a FontCache owned by the open editors (never in
        // static storage, so nothing font related outlives JUCE's shutdown when the plug-in unloads)
        struct FontCache
        {
            FontCache();
            ~FontCache();
            std::array<juce::Typeface::Ptr, 3> faces;
        };
        juce::Font font (float height, int weight = 0);    // 0 regular, 1 medium, 2 semibold
        juce::Typeface::Ptr typeface (int weight);

        // icons ----------------------------------------------------------------
        enum class Icon
        {
            Power, Close, Headphones, Ear, ChevronLeft, ChevronRight, ChevronDown, ChevronUp, Undo, Redo, Gear,
            Help, Piano, Brush, Match, Instances, Freeze, Scissors, Dynamic, Spectral, PhaseInvert, AutoGain,
            Plus, Sidechain, Expand, Link, Menu, Resize
        };
        void drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> r, juce::Colour c, float stroke = 1.5f);
        void drawShapeIcon (juce::Graphics& g, Shape s, juce::Rectangle<float> r, juce::Colour c, float stroke = 1.5f);
        void drawLogo (juce::Graphics& g, juce::Rectangle<float> r);

        // panel helpers
        void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, float corner, bool shadow = true);
        void drawGlowPath (juce::Graphics& g, const juce::Path& p, juce::Colour c, float width, float glow);
    }

    class ClaudeLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        ClaudeLookAndFeel();

        juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
        juce::Font getPopupMenuFont() override;
        void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
        void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                                const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour) override;
        void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>& area, const juce::String& sectionName) override;
        int getPopupMenuBorderSize() override { return 4; }
        juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
        void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
        void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool isScrollbarVertical,
                            int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown) override;
        void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
        void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
        juce::Font getAlertWindowTitleFont() override;
        juce::Font getAlertWindowMessageFont() override;
        juce::Font getAlertWindowFont() override;
        juce::Font getLabelFont (juce::Label&) override;
        void drawAlertBox (juce::Graphics&, juce::AlertWindow&, const juce::Rectangle<int>& textArea, juce::TextLayout&) override;
    };
}
