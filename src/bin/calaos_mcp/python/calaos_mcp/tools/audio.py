"""MCP tools: audio_control."""

from __future__ import annotations

_ALLOWED_ACTIONS = frozenset({
    "play", "pause", "stop", "next", "prev",
    "volume+", "volume-", "volume_set",
})


async def audio_control(client, player_id: str, action: str, **kwargs) -> dict:
    """Control an audio player.

    Args:
        player_id: The audio player ID.
        action: One of: play, pause, stop, next, prev, volume+, volume-, volume_set.
        volume: (optional, for volume_set) Integer 0-100.
    """
    if action not in _ALLOWED_ACTIONS:
        raise ValueError(
            f"Invalid action '{action}'. "
            f"Allowed actions: {sorted(_ALLOWED_ACTIONS)}"
        )
    result = await client.audio(player_id, action, **kwargs)
    return {"player_id": player_id, "action": action, "result": result}
