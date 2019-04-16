#pragma once
#include "CoreMinimal.h"
#include "JsonObjects.h"

#include "Animation/AnimSequence.h"

class AnimationBuilder{
public:
	void buildAnimation(UAnimSequence *animSequence, USkeleton *skeleton, const JsonAnimationClip &srcClip);
};
