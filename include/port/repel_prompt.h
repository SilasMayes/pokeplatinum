#ifndef POKEPLATINUM_PORT_REPEL_PROMPT_H
#define POKEPLATINUM_PORT_REPEL_PROMPT_H

// "Use another Repel?" (as in Black and White): when a Repel's effect wears off and the
// bag has another, the game asks instead of only saying so, and Yes uses one of the same
// kind as last time (or the weakest the bag has). Settings > Gameplay, on by default.

#include <nitro/types.h>

#include "field/field_system_decl.h"

#ifdef __cplusplus
extern "C" {
#endif

// The Repel ran out (Repel_UpdateSteps): TRUE if the question is being asked (a field task
// was started), FALSE to show the game's own message
BOOL RepelPrompt_Start(FieldSystem *fieldSystem);
// The bag used a Repel of this kind
void RepelPrompt_NoteUsed(u16 item);

#ifdef __cplusplus
}
#endif

#endif // POKEPLATINUM_PORT_REPEL_PROMPT_H
