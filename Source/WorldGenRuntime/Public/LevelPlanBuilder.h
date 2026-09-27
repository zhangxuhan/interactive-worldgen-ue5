#pragma once

#include "CoreMinimal.h"
#include "SceneSpec.h"

/**
 * M1b-1: deterministic level plan builder.
 *
 * Converts a *validated* FSceneSpec into an explicit, ordered list of build
 * operations (the "plan"). Every coordinate, size and rotation is fully
 * resolved here in C++ using the M1a coordinate convention — the Python level
 * executor only executes ops mechanically and performs no SceneSpec
 * interpretation of its own.
 *
 * Determinism: the plan is a pure function of the spec. Same JSON + seed
 * produces byte-identical plan text (fixed op order, fixed decimal format).
 *
 * Asset policy (updated in M2b, recorded in Docs/M2b_AssetIdPipeline.md):
 *  - Every object box op now carries the SceneSpec objects[].asset_id through
 *    to the plan JSON; the Python level executor resolves that asset_id
 *    against the AssetManifest (M2a) instead of guessing from the object id.
 *    Unregistered or not-ready asset_ids still build as the placeholder cube,
 *    with the object id, asset_id and fallback reason logged explicitly.
 *  - The exit door uses the fixed-size AWorldGenDoor placeholder
 *    (120 x 220 x 10 panel, hinge pivot): the spec door must be 120 cm wide
 *    and the room at least 220 cm high, else the plan reports an error.
 *  - on_top_of resolves to (support x, support y, support height) — the key
 *    sits on the desk top.
 *
 * Supported: one entrance, one locked exit with a door (on ANY of the four
 * walls - M2b multi-scene fixed the hardcoded north/south doorways), axis-
 * aligned room, objects with explicit position_cm or a single on_top_of hop
 * onto a positioned object. Exercised on north/south (workroom, scene B) and
 * east/west (scenes A/C of the M2b multi-scene AI experiment).
 */
class WORLDGENRUNTIME_API FLevelPlanBuilder
{
public:
	/**
	 * Build the plan text for a validated spec.
	 * Returns false and fills OutErrors when the spec cannot be realized with
	 * the M1b-1 placeholder set (caller must NOT generate a level then).
	 */
	static bool BuildPlan(const FSceneSpec& Spec, const FString& SourceSpecFileName,
		FString& OutPlanJson, TArray<FString>& OutErrors);
};
