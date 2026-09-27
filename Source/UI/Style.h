#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_core/juce_core.h>

namespace Style {
    // Background colors - MUST use 8-char hex (AARRGGBB) for full opacity
    const juce::Colour bgNearBlack   (0xFF0F090A);
    const juce::Colour bgCherryBlack (0xFF170E10);
    const juce::Colour bgBurgundy   (0xFF2A1317);

    // Cherry accent colors
    const juce::Colour cherryBright  (0xFFFF1744);
    const juce::Colour cherry        (0xFFE50935);
    const juce::Colour cherryDark    (0xFFB00024);
    const juce::Colour cherryWine    (0xFF5A0014);

    // Foreground text colors
    const juce::Colour fg            = juce::Colours::white;
    const juce::Colour fgSecondary   (0xFFDFD2D4);
    const juce::Colour fgMuted       (0xFF9E8C8F);

    // Status colors
    const juce::Colour warning       (0xFFFFC400);
    const juce::Colour success       (0xFF00E676);

    // Glass colors (intentionally semi-transparent)
    const juce::Colour glassBorder   (0x2EFFFFFF);
    const juce::Colour glassBgTop    (0x1AFFFFFF);
    const juce::Colour glassBgBottom (0x05FFFFFF);

    const juce::Colour cherryGlassBorder (0x47FF1744);

    inline void drawGlassPanel(juce::Graphics& g, juce::Rectangle<float> bounds, float radius = 14.0f, bool isCherry = false) {
        if (isCherry) {
            juce::ColourGradient bgGradient(cherryBright.withAlpha(0.18f), bounds.getCentreX(), 0.0f,
                                            juce::Colours::transparentBlack, bounds.getCentreX(), bounds.getHeight(), true);
            g.setGradientFill(bgGradient);
            g.fillRoundedRectangle(bounds, radius);

            g.setColour(cherryGlassBorder);
            g.drawRoundedRectangle(bounds.reduced(0.5f), radius, 1.0f);
        } else {
            juce::ColourGradient bgGradient(glassBgTop, 0.0f, 0.0f, glassBgBottom, 0.0f, bounds.getHeight(), false);
            g.setGradientFill(bgGradient);
            g.fillRoundedRectangle(bounds, radius);

            g.setColour(glassBorder);
            g.drawRoundedRectangle(bounds.reduced(0.5f), radius, 1.0f);
        }
    }
}
