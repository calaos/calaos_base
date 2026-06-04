"""Pydantic models for Calaos IO, Room, and Scenario objects."""

from __future__ import annotations

from typing import Any
from pydantic import BaseModel, Field


class IO(BaseModel):
    id: str
    name: str
    type: str = ""
    gui_type: str = ""
    var_type: str = "bool"
    visible: bool = True
    rw: bool = False
    unit: str = ""
    room: str = ""
    state: str = ""


class Room(BaseModel):
    name: str
    type: str = ""
    hits: int = 0
    ios: list[IO] = Field(default_factory=list)


class Scenario(BaseModel):
    id: str
    name: str
    visible: bool = True
    io_id: str = ""
    room: str = ""


class AudioPlayer(BaseModel):
    id: str
    name: str
    status: str = ""
