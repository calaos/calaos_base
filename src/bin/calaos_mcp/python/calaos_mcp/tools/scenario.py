"""MCP tools: list_scenarios and run_scenario."""

from __future__ import annotations

from calaos_mcp import safety
from calaos_mcp.tools._home import iter_ios

_SCENARIO_GUI_TYPES = frozenset({"scenario", "auto_scenario"})


async def list_scenarios(client) -> list[dict]:
    """Return all available scenarios (auto scenarios and manual scenarios)."""
    home = await client.get_home()
    result = []
    for io, room_name in iter_ios(home):
        if io.get("gui_type", "") in _SCENARIO_GUI_TYPES:
            result.append({
                "id": io.get("id", ""),
                "name": safety.safe_name(io.get("name", "")),
                "room": safety.safe_name(room_name),
                "gui_type": io.get("gui_type", ""),
            })
    return result


async def run_scenario(client, scenario_id: str) -> dict:
    """Trigger a scenario by its IO ID.

    Args:
        scenario_id: The IO ID of the scenario to run.
    """
    # Validate the ID exists and is actually a scenario
    home = await client.get_home()
    found = any(
        io.get("id") == scenario_id and io.get("gui_type", "") in _SCENARIO_GUI_TYPES
        for io, _ in iter_ios(home)
    )

    if not found:
        raise ValueError(
            f"Scenario '{scenario_id}' not found. "
            "Use list_scenarios to see available scenarios."
        )

    result = await client.set_state(scenario_id, "true")
    return {"scenario_id": scenario_id, "triggered": True, "result": result}
