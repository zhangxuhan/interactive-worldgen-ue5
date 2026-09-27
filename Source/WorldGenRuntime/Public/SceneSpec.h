#pragma once

#include "CoreMinimal.h"

/**
 * M1a: SceneSpec data contract (schema_version "0.1") + parser + validator.
 *
 * Units: centimeters and degrees. Coordinate system (room-local):
 *   - Room is an axis-aligned box centered on the origin:
 *       X in [-width/2, +width/2], Y in [-length/2, +length/2], Z in [0, height].
 *   - Walls: "north" = +X face, "south" = -X face, "east" = +Y face, "west" = -Y face.
 *       north/south walls run along Y (run-length = room length_cm),
 *       east/west walls run along X (run-length = room width_cm).
 *   - Door offset_cm is measured along the wall run from its negative end;
 *     the door spans [offset - width/2, offset + width/2].
 *   - Object placement.position_cm [x, y] is an absolute room-local position.
 *
 * Determinism: parsing and validation are pure functions of the input text.
 * Checks run in a fixed order and errors are appended in that order; no
 * hash-map iteration influences error output, so identical input always
 * produces identical results (verified twice per file by the M1a harness).
 */

struct WORLDGENRUNTIME_API FSceneSpecDoor
{
	FString Wall;            // north | south | east | west
	double OffsetCm = 0.0;
	double WidthCm = 0.0;
	bool bLocked = false;    // only meaningful for exit
};

struct WORLDGENRUNTIME_API FSceneSpecObject
{
	FString Id;
	FString SemanticType;
	FString AssetId;                 // optional in 0.1 (registry arrives with M2)
	double SizeXCm = 0.0;            // target_size_cm[0] along X before rotation
	double SizeYCm = 0.0;            // target_size_cm[1] along Y before rotation
	double SizeZCm = 0.0;            // target_size_cm[2] along Z
	bool bHasPosition = false;       // placement.position_cm present
	double PositionXCm = 0.0;
	double PositionYCm = 0.0;
	bool bOnTopOf = false;           // placement.on_top_of present
	FString OnTopOfId;
	double RotationDeg = 0.0;        // placement.rotation_deg
	FString CollisionPolicy;         // "" | none | simple | complex
	bool bNavObstacle = false;
	FString Priority;                // "" | required | optional
};

struct WORLDGENRUNTIME_API FSceneSpec
{
	FString SchemaVersion;
	FString SceneId;
	bool bSeedProvided = false;
	int64 Seed = 0;
	double RoomWidthCm = 0.0;
	double RoomLengthCm = 0.0;
	double RoomHeightCm = 0.0;
	FSceneSpecDoor Entrance;
	FSceneSpecDoor Exit;
	double AgentRadiusCm = 0.0;
	double AgentHeightCm = 0.0;
	TArray<FSceneSpecObject> Objects;
	FString ObjectiveType;
	FString ObjectiveItemId;

	/** Stable, deterministic one-line summary of the parsed spec (used by the
	 *  harness to verify that repeated parses agree). */
	FString MakeCanonicalSummary() const;
};

class WORLDGENRUNTIME_API FSceneSpecValidator
{
public:
	/**
	 * Parse and validate a SceneSpec JSON document.
	 * Returns true iff the document parses and passes every check.
	 * OutErrors entries are "field.path: reason" strings in a fixed order.
	 */
	static bool Validate(const FString& JsonText, FSceneSpec& OutSpec, TArray<FString>& OutErrors);

	/** File convenience wrapper. OutReadError is set when the file cannot be read. */
	static bool ValidateFile(const FString& AbsolutePath, FSceneSpec& OutSpec,
		TArray<FString>& OutErrors, FString& OutReadError);
};

namespace WorldGen
{
namespace SceneSpecTest
{
	/**
	 * M1a validation entry point (command line switch -M1aValidate).
	 * Runs every sample under Data/Scenes/{valid,invalid}, requires:
	 *   - valid samples:   zero errors, two parses agree
	 *   - invalid samples: >= 1 error matching the registered expected token,
	 *                      two parses agree
	 * Exits the process with code = failure count (0 = all as expected),
	 * using the same forced-exit path as the M0.5 suite.
	 */
	void RunM1aValidation();
}
}
