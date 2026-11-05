#pragma once

#include <LBAudio/UI.h>
#include "CompressorDSP.h"

namespace squeeze::ui
{

using namespace lbaudio::ui;

//==============================================================================
// Static input -> output curve, with a dot riding on it at the current input level.
class TransferCurve final : public juce::Component
{
public:
    void setState (const CompressorParams& p, float inputDb)
    {
        params = p;
        inputLevelDb = inputDb;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        g.setColour (Colours::panel);
        g.fillRoundedRectangle (area, 8.0f);
        g.setColour (Colours::panelEdge);
        g.drawRoundedRectangle (area.reduced (0.5f), 8.0f, 1.0f);

        const auto plot = area.reduced (14.0f).withTrimmedLeft (18.0f).withTrimmedBottom (16.0f);
        auto toX = [&] (float db) { return juce::jmap (db, minDb, maxDb, plot.getX(), plot.getRight()); };
        auto toY = [&] (float db) { return juce::jmap (db, minDb, maxDb, plot.getBottom(), plot.getY()); };

        // Grid + axis labels
        g.setFont (font (10.0f));
        for (float db = minDb; db <= maxDb + 0.01f; db += 12.0f)
        {
            g.setColour (Colours::panelEdge);
            g.drawHorizontalLine ((int) toY (db), plot.getX(), plot.getRight());
            g.drawVerticalLine ((int) toX (db), plot.getY(), plot.getBottom());

            g.setColour (Colours::dim.withAlpha (0.8f));
            const auto label = juce::String ((int) db);
            g.drawText (label, juce::Rectangle<float> (28.0f, 12.0f).withCentre ({ toX (db), plot.getBottom() + 10.0f }),
                        juce::Justification::centred);
            g.drawText (label, juce::Rectangle<float> (26.0f, 12.0f).withCentre ({ plot.getX() - 15.0f, toY (db) }),
                        juce::Justification::centredRight);
        }

        // Unity line
        juce::Path unity;
        unity.startNewSubPath (toX (minDb), toY (minDb));
        unity.lineTo (toX (maxDb), toY (maxDb));
        juce::Path dashed;
        const float dashes[] = { 4.0f, 4.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, unity, dashes, 2);
        g.setColour (Colours::dim.withAlpha (0.35f));
        g.fillPath (dashed);

        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (plot.toNearestInt());

        // Threshold marker + knee band
        const float tx = toX (params.thresholdDb);
        if (params.kneeDb > 0.0f)
        {
            g.setColour (Colours::accent.withAlpha (0.06f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (toX (params.thresholdDb - params.kneeDb * 0.5f), plot.getY(),
                                                                    toX (params.thresholdDb + params.kneeDb * 0.5f), plot.getBottom()));
        }
        g.setColour (Colours::accent.withAlpha (0.25f));
        g.drawVerticalLine ((int) tx, plot.getY(), plot.getBottom());

        // Curve
        juce::Path curve;
        const int steps = (int) plot.getWidth();
        for (int i = 0; i <= steps; ++i)
        {
            const float inDb = juce::jmap ((float) i, 0.0f, (float) steps, minDb, maxDb);
            const float y = toY (Compressor::staticOutputDb (inDb, params));
            if (i == 0) curve.startNewSubPath (toX (inDb), y);
            else        curve.lineTo (toX (inDb), y);
        }

        juce::Path fill (curve);
        fill.lineTo (plot.getRight(), plot.getBottom());
        fill.lineTo (plot.getX(), plot.getBottom());
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (Colours::accent.withAlpha (0.16f), 0.0f, plot.getY(),
                                                 Colours::accent.withAlpha (0.0f), 0.0f, plot.getBottom(), false));
        g.fillPath (fill);

        g.setColour (Colours::accent);
        g.strokePath (curve, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Live input dot
        if (inputLevelDb > minDb)
        {
            const juce::Point<float> p { toX (inputLevelDb), toY (Compressor::staticOutputDb (inputLevelDb, params)) };
            g.setColour (Colours::gr.withAlpha (0.25f));
            g.fillEllipse (juce::Rectangle<float> (16.0f, 16.0f).withCentre (p));
            g.setColour (Colours::gr);
            g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (p));
        }
    }

private:
    static constexpr float minDb = -60.0f, maxDb = 0.0f;
    CompressorParams params;
    float inputLevelDb = -100.0f;
};

} // namespace squeeze::ui
