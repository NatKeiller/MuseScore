/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore BVBA and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "playbackeventsrenderer.h"

#include "log.h"

#include "libmscore/chord.h"
#include "libmscore/rest.h"
#include "libmscore/note.h"

#include "utils/arrangementutils.h"
#include "metaparsers/chordarticulationsparser.h"
#include "metaparsers/notearticulationsparser.h"
#include "renderers/ornamentsrenderer.h"
#include "renderers/tremolorenderer.h"
#include "renderers/arpeggiorenderer.h"
#include "renderers/gracenotesrenderer.h"
#include "renderers/glissandosrenderer.h"

using namespace mu::engraving;
using namespace mu::mpe;

void PlaybackEventsRenderer::render(const Ms::EngravingItem* item, const dynamic_level_t nominalDynamicLevel,
                                    const ArticulationsProfilePtr profile,
                                    PlaybackEventsMap& result) const
{
    render(item, 0, nominalDynamicLevel, profile, result);
}

void PlaybackEventsRenderer::render(const Ms::EngravingItem* item, const int tickPositionOffset, const dynamic_level_t nominalDynamicLevel,
                                    const ArticulationsProfilePtr profile,
                                    PlaybackEventsMap& result) const
{
    TRACEFUNC;

    IF_ASSERT_FAILED(item->isChordRest()) {
        return;
    }

    if (item->type() == Ms::ElementType::CHORD) {
        renderNoteEvents(Ms::toChord(item), tickPositionOffset, nominalDynamicLevel, profile, result);
    } else if (item->type() == Ms::ElementType::REST) {
        renderRestEvents(Ms::toRest(item), tickPositionOffset, result);
    }
}

void PlaybackEventsRenderer::renderNoteEvents(const Ms::Chord* chord, const int tickPositionOffset,
                                              const mpe::dynamic_level_t nominalDynamicLevel,
                                              const mpe::ArticulationsProfilePtr profile, PlaybackEventsMap& result) const
{
    IF_ASSERT_FAILED(chord) {
        return;
    }

    int chordPosTick = chord->tick().ticks() + tickPositionOffset;
    int chordDurationTicks = chord->durationTypeTicks().ticks();
    BeatsPerSecond bps = chord->score()->tempomap()->tempo(chordPosTick);

    static ArticulationMap articulations;

    PlaybackContext ctx(timestampFromTicks(chord->score(), chordPosTick),
                        durationFromTicks(bps.val, chordDurationTicks),
                        nominalDynamicLevel,
                        chordPosTick,
                        chordDurationTicks,
                        bps,
                        articulations,
                        profile);

    ChordArticulationsParser::buildChordArticulationMap(chord, ctx, ctx.commonArticulations);

    renderArticulations(chord, ctx, result[ctx.nominalTimestamp]);
}

void PlaybackEventsRenderer::renderRestEvents(const Ms::Rest* rest, const int tickPositionOffset, mpe::PlaybackEventsMap& result) const
{
    IF_ASSERT_FAILED(rest) {
        return;
    }

    int positionTick = rest->tick().ticks() + tickPositionOffset;
    int durationTicks = rest->durationTypeTicks().ticks();
    qreal beatsPerSecond = rest->score()->tempomap()->tempo(positionTick).val;

    timestamp_t nominalTimestamp = timestampFromTicks(rest->score(), positionTick);
    duration_t nominalDuration = durationFromTicks(beatsPerSecond, durationTicks);

    result[nominalTimestamp].push_back(mpe::RestEvent(nominalTimestamp, nominalDuration, rest->voice()));
}

void PlaybackEventsRenderer::renderArticulations(const Ms::Chord* chord, const PlaybackContext& ctx, mpe::PlaybackEventList& result) const
{
    if (renderChordArticulations(chord, ctx, result)) {
        return;
    }

    renderNoteArticulations(chord, ctx, result);
}

bool PlaybackEventsRenderer::renderChordArticulations(const Ms::Chord* chord, const PlaybackContext& ctx,
                                                      mpe::PlaybackEventList& result) const
{
    for (const auto& pair : ctx.commonArticulations) {
        const ArticulationType type = pair.first;

        if (OrnamentsRenderer::isAbleToRender(type)) {
            OrnamentsRenderer::render(chord, type, ctx, result);
            return true;
        }

        if (TremoloRenderer::isAbleToRender(type)) {
            TremoloRenderer::render(chord, type, ctx, result);
            return true;
        }

        if (ArpeggioRenderer::isAbleToRender(type)) {
            ArpeggioRenderer::render(chord, type, ctx, result);
            return true;
        }

        if (GraceNotesRenderer::isAbleToRender(type)) {
            GraceNotesRenderer::render(chord, type, ctx, result);
            return true;
        }
    }

    return false;
}

void PlaybackEventsRenderer::renderNoteArticulations(const Ms::Chord* chord, const PlaybackContext& ctx,
                                                     mpe::PlaybackEventList& result) const
{
    for (const Ms::Note* note : chord->notes()) {
        NominalNoteCtx noteCtx(note, ctx);

        NoteArticulationsParser::buildNoteArticulationMap(note, ctx, noteCtx.chordCtx.commonArticulations);

        if (noteCtx.chordCtx.commonArticulations.contains(ArticulationType::DiscreteGlissando)) {
            GlissandosRenderer::render(note, ArticulationType::DiscreteGlissando, noteCtx.chordCtx, result);
            continue;
        }

        if (noteCtx.chordCtx.commonArticulations.contains(ArticulationType::ContinuousGlissando)) {
            GlissandosRenderer::render(note, ArticulationType::ContinuousGlissando, noteCtx.chordCtx, result);
            continue;
        }

        result.emplace_back(buildNoteEvent(std::move(noteCtx)));
    }
}
