/*
    Claude-Q 4 - visual theme
*/
#include "Theme.h"
#include "ClaudeQBinaryData.h"

namespace cq
{
    namespace theme
    {
        juce::Colour bandColour (int band)
        {
            // 24 distinct hues, neighbours far apart on the colour wheel
            static const juce::uint32 palette[] =
            {
                0xffff6b6b, 0xff4ec9f0, 0xffb98cff, 0xff5fd99a, 0xffffa54d, 0xfff06fc4,
                0xff7aa7ff, 0xffd9e05a, 0xff4fe0d0, 0xffff8f8f, 0xff9b7bff, 0xff6fdc6f,
                0xffffc35c, 0xffe57bf0, 0xff58b8ff, 0xffb6e86a, 0xffff7aa2, 0xff6ee6ff,
                0xffc69bff, 0xff8ff0b0, 0xffffb07a, 0xfff593d8, 0xff8db8ff, 0xffe8e87a
            };
            return juce::Colour (palette[((band % 24) + 24) % 24]);
        }

        juce::Colour placementColour (Placement p)
        {
            switch (p)
            {
                case Placement::Left:  return juce::Colour (0xff6fb6ff);
                case Placement::Right: return juce::Colour (0xffff7f8a);
                case Placement::Mid:   return juce::Colour (0xff86e08c);
                case Placement::Side:  return juce::Colour (0xffc58bff);
                default: break;
            }
            return accent;
        }

        namespace
        {
            FontCache* currentFontCache = nullptr;
        }

        FontCache::FontCache()
        {
            faces[0] = juce::Typeface::createSystemTypefaceFor (ClaudeQBinaryData::InterRegular_otf,  (size_t) ClaudeQBinaryData::InterRegular_otfSize);
            faces[1] = juce::Typeface::createSystemTypefaceFor (ClaudeQBinaryData::InterMedium_otf,   (size_t) ClaudeQBinaryData::InterMedium_otfSize);
            faces[2] = juce::Typeface::createSystemTypefaceFor (ClaudeQBinaryData::InterSemiBold_otf, (size_t) ClaudeQBinaryData::InterSemiBold_otfSize);
            currentFontCache = this;
        }

        FontCache::~FontCache()
        {
            if (currentFontCache == this) currentFontCache = nullptr;
        }

        juce::Typeface::Ptr typeface (int weight)
        {
            if (currentFontCache == nullptr) return nullptr;
            return currentFontCache->faces[(size_t) juce::jlimit (0, 2, weight)];
        }

        juce::Font font (float height, int weight)
        {
            if (auto tf = typeface (weight)) return juce::Font (juce::FontOptions (tf).withHeight (height));
            return juce::Font (juce::FontOptions (height, weight >= 2 ? juce::Font::bold : juce::Font::plain));
        }

        //======================================================================
        namespace
        {
            struct Unit
            {
                juce::Rectangle<float> r;
                juce::Point<float> p (float x, float y) const { return { r.getX() + x * r.getWidth(), r.getY() + y * r.getHeight() }; }
                float s (float v) const { return v * std::min (r.getWidth(), r.getHeight()); }
            };

            void line (juce::Path& path, const Unit& u, float x0, float y0, float x1, float y1)
            {
                path.startNewSubPath (u.p (x0, y0));
                path.lineTo (u.p (x1, y1));
            }
        }

        void drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> rect, juce::Colour c, float stroke)
        {
            // square drawing area centred in rect
            const float side = std::min (rect.getWidth(), rect.getHeight());
            const Unit u { juce::Rectangle<float> (side, side).withCentre (rect.getCentre()) };
            juce::Path p;
            juce::PathStrokeType st (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
            g.setColour (c);
            const auto cen = u.r.getCentre();
            switch (icon)
            {
                case Icon::Power:
                {
                    const float rad = u.s (0.34f);
                    p.addCentredArc (cen.x, cen.y + u.s (0.04f), rad, rad, 0.0f, juce::degreesToRadians (35.0f), juce::degreesToRadians (325.0f), true);
                    line (p, u, 0.5f, 0.1f, 0.5f, 0.46f);
                    break;
                }
                case Icon::Close:
                    line (p, u, 0.28f, 0.28f, 0.72f, 0.72f);
                    line (p, u, 0.72f, 0.28f, 0.28f, 0.72f);
                    break;
                case Icon::Plus:
                    line (p, u, 0.5f, 0.22f, 0.5f, 0.78f);
                    line (p, u, 0.22f, 0.5f, 0.78f, 0.5f);
                    break;
                case Icon::Headphones:
                {
                    const float rad = u.s (0.32f);
                    p.addCentredArc (cen.x, cen.y + u.s (0.06f), rad, rad, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
                    g.strokePath (p, st);
                    p.clear();
                    g.fillRoundedRectangle (juce::Rectangle<float> (u.p (0.12f, 0.52f), u.p (0.3f, 0.86f)), u.s (0.05f));
                    g.fillRoundedRectangle (juce::Rectangle<float> (u.p (0.7f, 0.52f), u.p (0.88f, 0.86f)), u.s (0.05f));
                    return;
                }
                case Icon::Ear:
                {
                    p.startNewSubPath (u.p (0.36f, 0.42f));
                    p.cubicTo (u.p (0.34f, 0.12f), u.p (0.78f, 0.1f), u.p (0.74f, 0.42f));
                    p.cubicTo (u.p (0.72f, 0.58f), u.p (0.56f, 0.6f), u.p (0.56f, 0.74f));
                    p.cubicTo (u.p (0.56f, 0.88f), u.p (0.4f, 0.92f), u.p (0.34f, 0.8f));
                    p.startNewSubPath (u.p (0.48f, 0.42f));
                    p.cubicTo (u.p (0.48f, 0.3f), u.p (0.62f, 0.3f), u.p (0.62f, 0.42f));
                    break;
                }
                case Icon::ChevronLeft:  p.startNewSubPath (u.p (0.6f, 0.25f)); p.lineTo (u.p (0.36f, 0.5f)); p.lineTo (u.p (0.6f, 0.75f)); break;
                case Icon::ChevronRight: p.startNewSubPath (u.p (0.4f, 0.25f)); p.lineTo (u.p (0.64f, 0.5f)); p.lineTo (u.p (0.4f, 0.75f)); break;
                case Icon::ChevronDown:  p.startNewSubPath (u.p (0.27f, 0.4f)); p.lineTo (u.p (0.5f, 0.63f)); p.lineTo (u.p (0.73f, 0.4f)); break;
                case Icon::ChevronUp:    p.startNewSubPath (u.p (0.27f, 0.6f)); p.lineTo (u.p (0.5f, 0.37f)); p.lineTo (u.p (0.73f, 0.6f)); break;
                case Icon::Undo:
                case Icon::Redo:
                {
                    const bool redo = icon == Icon::Redo;
                    auto X = [redo] (float x) { return redo ? 1.0f - x : x; };
                    p.startNewSubPath (u.p (X (0.26f), 0.42f));
                    p.lineTo (u.p (X (0.6f), 0.42f));
                    p.cubicTo (u.p (X (0.84f), 0.42f), u.p (X (0.84f), 0.78f), u.p (X (0.6f), 0.78f));
                    p.lineTo (u.p (X (0.42f), 0.78f));
                    p.startNewSubPath (u.p (X (0.4f), 0.26f));
                    p.lineTo (u.p (X (0.24f), 0.42f));
                    p.lineTo (u.p (X (0.4f), 0.58f));
                    break;
                }
                case Icon::Gear:
                {
                    const int teeth = 8;
                    const float ro = u.s (0.42f), ri = u.s (0.31f);
                    juce::Path gp;
                    for (int i = 0; i < teeth * 4; ++i)
                    {
                        const float a = juce::MathConstants<float>::twoPi * (float) i / (float) (teeth * 4);
                        const float rr = ((i / 2) % 2 == 0) ? ro : ri;
                        const juce::Point<float> q (cen.x + rr * std::sin (a), cen.y - rr * std::cos (a));
                        if (i == 0) gp.startNewSubPath (q); else gp.lineTo (q);
                    }
                    gp.closeSubPath();
                    gp.addEllipse (cen.x - u.s (0.13f), cen.y - u.s (0.13f), u.s (0.26f), u.s (0.26f));
                    gp.setUsingNonZeroWinding (false);
                    g.fillPath (gp);
                    return;
                }
                case Icon::Help:
                    g.setFont (font (u.s (0.78f), 2));
                    g.drawText ("?", u.r, juce::Justification::centred, false);
                    return;
                case Icon::Piano:
                {
                    const auto box = juce::Rectangle<float> (u.p (0.14f, 0.24f), u.p (0.86f, 0.78f));
                    g.drawRoundedRectangle (box, u.s (0.06f), stroke * 0.9f);
                    for (int k = 1; k < 4; ++k)
                    {
                        const float x = box.getX() + box.getWidth() * (float) k / 4.0f;
                        g.drawLine (x, box.getY() + box.getHeight() * 0.55f, x, box.getBottom(), stroke * 0.7f);
                    }
                    for (int k = 0; k < 3; ++k)
                    {
                        if (k == 1) continue;
                        const float x = box.getX() + box.getWidth() * ((float) k + 0.72f) / 4.0f;
                        g.fillRect (juce::Rectangle<float> (x, box.getY(), box.getWidth() * 0.13f, box.getHeight() * 0.56f));
                    }
                    return;
                }
                case Icon::Brush:
                {
                    line (p, u, 0.8f, 0.18f, 0.44f, 0.56f);
                    g.strokePath (p, st);
                    juce::Path tip;
                    tip.startNewSubPath (u.p (0.44f, 0.5f));
                    tip.cubicTo (u.p (0.3f, 0.48f), u.p (0.22f, 0.66f), u.p (0.16f, 0.84f));
                    tip.cubicTo (u.p (0.36f, 0.82f), u.p (0.52f, 0.74f), u.p (0.5f, 0.58f));
                    tip.closeSubPath();
                    g.fillPath (tip);
                    return;
                }
                case Icon::Match:
                {
                    p.startNewSubPath (u.p (0.1f, 0.42f));
                    p.cubicTo (u.p (0.3f, 0.12f), u.p (0.45f, 0.62f), u.p (0.62f, 0.38f));
                    p.cubicTo (u.p (0.72f, 0.24f), u.p (0.82f, 0.32f), u.p (0.9f, 0.34f));
                    g.strokePath (p, st);
                    juce::Path q;
                    q.startNewSubPath (u.p (0.1f, 0.66f));
                    q.cubicTo (u.p (0.3f, 0.4f), u.p (0.45f, 0.86f), u.p (0.62f, 0.62f));
                    q.cubicTo (u.p (0.72f, 0.48f), u.p (0.82f, 0.56f), u.p (0.9f, 0.58f));
                    g.setColour (c.withMultipliedAlpha (0.55f));
                    g.strokePath (q, st);
                    return;
                }
                case Icon::Instances:
                    for (int k = 0; k < 3; ++k)
                        g.drawRoundedRectangle (juce::Rectangle<float> (u.p (0.16f, 0.2f + 0.22f * (float) k), u.p (0.84f, 0.34f + 0.22f * (float) k)), u.s (0.04f), stroke * 0.85f);
                    return;
                case Icon::Freeze:
                    for (int k = 0; k < 3; ++k)
                    {
                        const float a = juce::MathConstants<float>::pi * (float) k / 3.0f;
                        const float rr = u.s (0.36f);
                        p.startNewSubPath (cen.x - rr * std::cos (a), cen.y - rr * std::sin (a));
                        p.lineTo (cen.x + rr * std::cos (a), cen.y + rr * std::sin (a));
                    }
                    break;
                case Icon::Scissors:
                {
                    g.drawEllipse (juce::Rectangle<float> (u.p (0.14f, 0.6f), u.p (0.36f, 0.82f)), stroke);
                    g.drawEllipse (juce::Rectangle<float> (u.p (0.64f, 0.6f), u.p (0.86f, 0.82f)), stroke);
                    line (p, u, 0.32f, 0.62f, 0.66f, 0.16f);
                    line (p, u, 0.68f, 0.62f, 0.34f, 0.16f);
                    break;
                }
                case Icon::Dynamic:
                {
                    p.startNewSubPath (u.p (0.08f, 0.66f));
                    p.cubicTo (u.p (0.38f, 0.66f), u.p (0.38f, 0.3f), u.p (0.5f, 0.3f));
                    p.cubicTo (u.p (0.62f, 0.3f), u.p (0.62f, 0.66f), u.p (0.92f, 0.66f));
                    g.strokePath (p, st);
                    juce::Path a;
                    a.addTriangle (u.p (0.5f, 0.05f), u.p (0.42f, 0.17f), u.p (0.58f, 0.17f));
                    a.addTriangle (u.p (0.5f, 0.95f), u.p (0.42f, 0.83f), u.p (0.58f, 0.83f));
                    g.fillPath (a);
                    return;
                }
                case Icon::Spectral:
                {
                    const float hs[] = { 0.3f, 0.55f, 0.8f, 0.6f, 0.42f };
                    for (int k = 0; k < 5; ++k)
                    {
                        const float x = 0.16f + 0.16f * (float) k;
                        line (p, u, x, 0.84f, x, 0.84f - 0.62f * hs[k]);
                    }
                    break;
                }
                case Icon::PhaseInvert:
                    g.drawEllipse (juce::Rectangle<float> (u.p (0.22f, 0.22f), u.p (0.78f, 0.78f)), stroke);
                    line (p, u, 0.2f, 0.8f, 0.8f, 0.2f);
                    break;
                case Icon::AutoGain:
                {
                    line (p, u, 0.18f, 0.78f, 0.82f, 0.78f);
                    line (p, u, 0.5f, 0.18f, 0.5f, 0.66f);
                    p.startNewSubPath (u.p (0.36f, 0.32f)); p.lineTo (u.p (0.5f, 0.18f)); p.lineTo (u.p (0.64f, 0.32f));
                    p.startNewSubPath (u.p (0.36f, 0.52f)); p.lineTo (u.p (0.5f, 0.66f)); p.lineTo (u.p (0.64f, 0.52f));
                    break;
                }
                case Icon::Sidechain:
                {
                    g.drawRoundedRectangle (juce::Rectangle<float> (u.p (0.44f, 0.22f), u.p (0.86f, 0.78f)), u.s (0.06f), stroke);
                    line (p, u, 0.1f, 0.5f, 0.6f, 0.5f);
                    p.startNewSubPath (u.p (0.48f, 0.38f)); p.lineTo (u.p (0.6f, 0.5f)); p.lineTo (u.p (0.48f, 0.62f));
                    break;
                }
                case Icon::Expand:
                    p.startNewSubPath (u.p (0.28f, 0.52f)); p.lineTo (u.p (0.5f, 0.3f)); p.lineTo (u.p (0.72f, 0.52f));
                    p.startNewSubPath (u.p (0.28f, 0.74f)); p.lineTo (u.p (0.5f, 0.52f)); p.lineTo (u.p (0.72f, 0.74f));
                    break;
                case Icon::Link:
                    g.drawRoundedRectangle (juce::Rectangle<float> (u.p (0.1f, 0.36f), u.p (0.56f, 0.64f)), u.s (0.14f), stroke);
                    g.drawRoundedRectangle (juce::Rectangle<float> (u.p (0.44f, 0.36f), u.p (0.9f, 0.64f)), u.s (0.14f), stroke);
                    return;
                case Icon::Menu:
                    line (p, u, 0.2f, 0.3f, 0.8f, 0.3f);
                    line (p, u, 0.2f, 0.5f, 0.8f, 0.5f);
                    line (p, u, 0.2f, 0.7f, 0.8f, 0.7f);
                    break;
                case Icon::Resize:
                    line (p, u, 0.3f, 0.9f, 0.9f, 0.3f);
                    line (p, u, 0.6f, 0.9f, 0.9f, 0.6f);
                    break;
            }
            g.strokePath (p, st);
        }

        void drawShapeIcon (juce::Graphics& g, Shape s, juce::Rectangle<float> rect, juce::Colour c, float stroke)
        {
            const Unit u { rect };
            juce::Path p;
            switch (s)
            {
                case Shape::Bell:
                    p.startNewSubPath (u.p (0.0f, 0.72f));
                    p.cubicTo (u.p (0.32f, 0.72f), u.p (0.36f, 0.18f), u.p (0.5f, 0.18f));
                    p.cubicTo (u.p (0.64f, 0.18f), u.p (0.68f, 0.72f), u.p (1.0f, 0.72f));
                    break;
                case Shape::LowShelf:
                    p.startNewSubPath (u.p (0.0f, 0.28f));
                    p.lineTo (u.p (0.3f, 0.28f));
                    p.cubicTo (u.p (0.5f, 0.28f), u.p (0.5f, 0.72f), u.p (0.7f, 0.72f));
                    p.lineTo (u.p (1.0f, 0.72f));
                    break;
                case Shape::HighShelf:
                    p.startNewSubPath (u.p (0.0f, 0.72f));
                    p.lineTo (u.p (0.3f, 0.72f));
                    p.cubicTo (u.p (0.5f, 0.72f), u.p (0.5f, 0.28f), u.p (0.7f, 0.28f));
                    p.lineTo (u.p (1.0f, 0.28f));
                    break;
                case Shape::LowCut:
                    p.startNewSubPath (u.p (0.08f, 0.95f));
                    p.cubicTo (u.p (0.25f, 0.5f), u.p (0.4f, 0.32f), u.p (0.62f, 0.32f));
                    p.lineTo (u.p (1.0f, 0.32f));
                    break;
                case Shape::HighCut:
                    p.startNewSubPath (u.p (0.0f, 0.32f));
                    p.lineTo (u.p (0.38f, 0.32f));
                    p.cubicTo (u.p (0.6f, 0.32f), u.p (0.75f, 0.5f), u.p (0.92f, 0.95f));
                    break;
                case Shape::Notch:
                    p.startNewSubPath (u.p (0.0f, 0.3f));
                    p.lineTo (u.p (0.36f, 0.3f));
                    p.cubicTo (u.p (0.46f, 0.3f), u.p (0.47f, 0.92f), u.p (0.5f, 0.92f));
                    p.cubicTo (u.p (0.53f, 0.92f), u.p (0.54f, 0.3f), u.p (0.64f, 0.3f));
                    p.lineTo (u.p (1.0f, 0.3f));
                    break;
                case Shape::BandPass:
                    p.startNewSubPath (u.p (0.06f, 0.95f));
                    p.cubicTo (u.p (0.3f, 0.6f), u.p (0.38f, 0.25f), u.p (0.5f, 0.25f));
                    p.cubicTo (u.p (0.62f, 0.25f), u.p (0.7f, 0.6f), u.p (0.94f, 0.95f));
                    break;
                case Shape::TiltShelf:
                    p.startNewSubPath (u.p (0.0f, 0.78f));
                    p.lineTo (u.p (0.25f, 0.78f));
                    p.cubicTo (u.p (0.5f, 0.78f), u.p (0.5f, 0.22f), u.p (0.75f, 0.22f));
                    p.lineTo (u.p (1.0f, 0.22f));
                    break;
                case Shape::FlatTilt:
                    p.startNewSubPath (u.p (0.0f, 0.82f));
                    p.lineTo (u.p (1.0f, 0.18f));
                    break;
                case Shape::AllPass:
                {
                    p.startNewSubPath (u.p (0.0f, 0.5f));
                    p.lineTo (u.p (0.3f, 0.5f));
                    p.startNewSubPath (u.p (0.7f, 0.5f));
                    p.lineTo (u.p (1.0f, 0.5f));
                    const auto cc = u.p (0.5f, 0.5f);
                    p.addCentredArc (cc.x, cc.y, u.s (0.2f), u.s (0.2f), 0.0f, 0.4f, 5.9f, true);
                    break;
                }
                default: break;
            }
            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        void drawLogo (juce::Graphics& g, juce::Rectangle<float> r)
        {
            // glyph: rounded square with an EQ curve
            const float h = r.getHeight();
            auto glyph = juce::Rectangle<float> (r.getX(), r.getY() + h * 0.12f, h * 0.76f, h * 0.76f);
            juce::ColourGradient grad (juce::Colour (0xfff7b955), glyph.getX(), glyph.getY(),
                                       juce::Colour (0xffd9733a), glyph.getRight(), glyph.getBottom(), false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (glyph, h * 0.18f);
            juce::Path curve;
            const Unit u { glyph.reduced (glyph.getWidth() * 0.16f) };
            curve.startNewSubPath (u.p (0.0f, 0.62f));
            curve.cubicTo (u.p (0.22f, 0.62f), u.p (0.28f, 0.12f), u.p (0.44f, 0.12f));
            curve.cubicTo (u.p (0.6f, 0.12f), u.p (0.6f, 0.86f), u.p (0.76f, 0.86f));
            curve.cubicTo (u.p (0.88f, 0.86f), u.p (0.92f, 0.5f), u.p (1.0f, 0.5f));
            g.setColour (juce::Colour (0xff15161c));
            g.strokePath (curve, juce::PathStrokeType (h * 0.085f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            // word mark
            auto textArea = r.withLeft (glyph.getRight() + h * 0.28f);
            const auto f1 = font (h * 0.62f, 2);
            g.setFont (f1);
            g.setColour (text);
            const juce::String word = "Claude-Q";
            const float w = juce::GlyphArrangement::getStringWidth (f1, word);
            g.drawText (word, textArea.withWidth (w + 2.0f), juce::Justification::centredLeft, false);
            g.setFont (font (h * 0.44f, 2));
            g.setColour (accent);
            g.drawText ("4", juce::Rectangle<float> (textArea.getX() + w + h * 0.06f, r.getY() - h * 0.04f, h * 0.5f, h * 0.6f),
                        juce::Justification::centredLeft, false);
        }

        void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, float corner, bool shadow)
        {
            if (shadow)
            {
                for (int i = 4; i >= 1; --i)
                {
                    g.setColour (juce::Colours::black.withAlpha (0.07f));
                    g.fillRoundedRectangle (r.expanded ((float) i * 2.0f).translated (0.0f, (float) i), corner + (float) i * 2.0f);
                }
            }
            juce::ColourGradient grad (juce::Colour (0xf5232633), r.getX(), r.getY(), juce::Colour (0xf5181a23), r.getX(), r.getBottom(), false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (r, corner);
            g.setColour (panelEdge);
            g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
            g.setColour (juce::Colours::white.withAlpha (0.04f));
            g.drawLine (r.getX() + corner, r.getY() + 1.5f, r.getRight() - corner, r.getY() + 1.5f, 1.0f);
        }

        void drawGlowPath (juce::Graphics& g, const juce::Path& p, juce::Colour c, float width, float glow)
        {
            if (glow > 0.0f)
            {
                g.setColour (c.withMultipliedAlpha (0.10f));
                g.strokePath (p, juce::PathStrokeType (width + glow * 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour (c.withMultipliedAlpha (0.18f));
                g.strokePath (p, juce::PathStrokeType (width + glow, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    //==========================================================================
    ClaudeLookAndFeel::ClaudeLookAndFeel()
    {
        using namespace theme;
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1b1d27));
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.18f));
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour (juce::PopupMenu::headerTextColourId, textDim);
        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff20232e));
        setColour (juce::TooltipWindow::textColourId, text);
        setColour (juce::TooltipWindow::outlineColourId, panelEdge);
        setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff12131a));
        setColour (juce::TextEditor::textColourId, text);
        setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.35f));
        setColour (juce::TextEditor::outlineColourId, panelEdge);
        setColour (juce::TextEditor::focusedOutlineColourId, accent);
        setColour (juce::CaretComponent::caretColourId, accent);
        setColour (juce::Label::textColourId, text);
        setColour (juce::TextButton::buttonColourId, controlBg);
        setColour (juce::TextButton::buttonOnColourId, accent.withAlpha (0.3f));
        setColour (juce::TextButton::textColourOffId, text);
        setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        setColour (juce::AlertWindow::backgroundColourId, juce::Colour (0xff1b1d27));
        setColour (juce::AlertWindow::textColourId, text);
        setColour (juce::AlertWindow::outlineColourId, panelEdge);
        setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xff3a3e50));
        setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::ResizableWindow::backgroundColourId, bgBottom);
    }

    juce::Typeface::Ptr ClaudeLookAndFeel::getTypefaceForFont (const juce::Font& f)
    {
        if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
            if (auto tf = theme::typeface (f.isBold() ? 2 : 0))
                return tf;
        return juce::LookAndFeel_V4::getTypefaceForFont (f);
    }

    juce::Font ClaudeLookAndFeel::getPopupMenuFont() { return theme::font (14.0f, 0); }

    void ClaudeLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
    {
        g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
        g.setColour (theme::panelEdge);
        g.drawRect (0, 0, width, height, 1);
    }

    void ClaudeLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                              bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                                              const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour)
    {
        if (isSeparator)
        {
            g.setColour (theme::panelEdge);
            g.fillRect (area.reduced (8, 0).withHeight (1).withY (area.getCentreY()));
            return;
        }
        auto r = area.reduced (2, 0);
        if (isHighlighted && isActive)
        {
            g.setColour (findColour (juce::PopupMenu::highlightedBackgroundColourId));
            g.fillRoundedRectangle (r.toFloat(), 3.0f);
        }
        auto c = textColour != nullptr ? *textColour : findColour (juce::PopupMenu::textColourId);
        if (! isActive) c = c.withAlpha (0.35f);
        if (isHighlighted && isActive) c = juce::Colours::white;
        auto textArea = r.reduced (10, 0);
        const auto tickArea = textArea.removeFromLeft (16).toFloat();
        if (isTicked)
        {
            g.setColour (theme::accent);
            g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (tickArea.getCentre()));
        }
        if (icon != nullptr)
            icon->drawWithin (g, textArea.removeFromLeft (r.getHeight()).toFloat().reduced (3.0f), juce::RectanglePlacement::centred, 1.0f);
        g.setFont (getPopupMenuFont());
        g.setColour (c);
        g.drawFittedText (text, textArea, juce::Justification::centredLeft, 1);
        if (shortcutKeyText.isNotEmpty())
        {
            g.setFont (theme::font (12.0f));
            g.setColour (theme::textDim);
            g.drawText (shortcutKeyText, textArea, juce::Justification::centredRight, true);
        }
        if (hasSubMenu)
            theme::drawIcon (g, theme::Icon::ChevronRight, textArea.removeFromRight (12).toFloat(), c, 1.3f);
    }

    void ClaudeLookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& sectionName)
    {
        g.setFont (theme::font (11.5f, 2));
        g.setColour (theme::textDim);
        g.drawFittedText (sectionName.toUpperCase(), area.reduced (12, 0), juce::Justification::bottomLeft, 1);
    }

    juce::Rectangle<int> ClaudeLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea)
    {
        const auto f = theme::font (13.0f);
        const int w = (int) juce::GlyphArrangement::getStringWidth (f, tipText) + 20;
        const int h = 26;
        return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 18,
                                     screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 8) : screenPos.y + 18,
                                     w, h).constrainedWithin (parentArea);
    }

    void ClaudeLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
    {
        const auto r = juce::Rectangle<float> ((float) width, (float) height);
        g.setColour (findColour (juce::TooltipWindow::backgroundColourId));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (theme::panelEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
        g.setColour (theme::text);
        g.setFont (theme::font (13.0f));
        g.drawText (text, r.reduced (10.0f, 0.0f), juce::Justification::centredLeft, true);
    }

    void ClaudeLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height, bool vertical,
                                           int thumbStart, int thumbSize, bool isMouseOver, bool)
    {
        juce::Rectangle<float> thumb = vertical ? juce::Rectangle<float> ((float) x + 2.0f, (float) thumbStart, (float) width - 4.0f, (float) thumbSize)
                                                : juce::Rectangle<float> ((float) thumbStart, (float) y + 2.0f, (float) thumbSize, (float) height - 4.0f);
        g.setColour (juce::Colour (isMouseOver ? 0xff4a4f66 : 0xff343849));
        g.fillRoundedRectangle (thumb, 3.0f);
    }

    void ClaudeLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& te)
    {
        g.setColour (te.findColour (juce::TextEditor::backgroundColourId));
        g.fillRoundedRectangle (juce::Rectangle<float> ((float) width, (float) height), 3.0f);
    }

    void ClaudeLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& te)
    {
        g.setColour (te.hasKeyboardFocus (true) ? theme::accent : theme::panelEdge);
        g.drawRoundedRectangle (juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f), 3.0f, 1.0f);
    }

    void ClaudeLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg, bool over, bool down)
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        auto c = b.getToggleState() ? theme::accent.withAlpha (0.25f) : bg;
        if (over) c = c.brighter (0.08f);
        if (down) c = c.brighter (0.15f);
        g.setColour (c);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (theme::panelEdge);
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
    }

    juce::Font ClaudeLookAndFeel::getTextButtonFont (juce::TextButton&, int h) { return theme::font (std::min (14.0f, (float) h * 0.55f), 1); }
    juce::Font ClaudeLookAndFeel::getAlertWindowTitleFont()   { return theme::font (17.0f, 2); }
    juce::Font ClaudeLookAndFeel::getAlertWindowMessageFont() { return theme::font (14.0f); }
    juce::Font ClaudeLookAndFeel::getAlertWindowFont()        { return theme::font (14.0f); }
    juce::Font ClaudeLookAndFeel::getLabelFont (juce::Label&) { return theme::font (14.0f); }

    void ClaudeLookAndFeel::drawAlertBox (juce::Graphics& g, juce::AlertWindow& alert, const juce::Rectangle<int>& textArea, juce::TextLayout& layout)
    {
        auto r = alert.getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff1b1d27));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (theme::panelEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
        layout.draw (g, textArea.toFloat());
    }
}
