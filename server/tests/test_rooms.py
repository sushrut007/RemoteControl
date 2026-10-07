"""Room store unit tests."""

import pytest

from server import (
    FORBIDDEN,
    INVALID_PIN,
    ROOM_FULL,
    ROOM_NOT_FOUND,
    ControlState,
    Role,
    RoomStore,
    SignalingError,
    hash_pin,
    verify_pin,
)


@pytest.fixture
def store() -> RoomStore:
    return RoomStore(max_room_size=2)


def test_create_room_host_role(store: RoomStore) -> None:
    room = store.create_room("dev-a", "Alice")
    assert room.host_device_id == "dev-a"
    assert room.members["dev-a"].role == Role.HOST
    assert store.device_room_id("dev-a") == room.room_id


def test_join_without_pin(store: RoomStore) -> None:
    room = store.create_room("dev-a", "Alice")
    joined, member = store.join_room("dev-b", "Bob", room.room_id)
    assert member.role == Role.CONTROLLER
    assert len(joined.members) == 2


def test_join_with_pin(store: RoomStore) -> None:
    room = store.create_room("dev-a", "Alice", pin="secret")
    with pytest.raises(SignalingError) as exc:
        store.join_room("dev-b", "Bob", room.room_id)
    assert exc.value.code == INVALID_PIN

    _, member = store.join_room("dev-b", "Bob", room.room_id, pin="secret")
    assert member.role == Role.CONTROLLER


def test_host_controller_pair_blocks_third_join(store: RoomStore) -> None:
    room = store.create_room("dev-a", "Alice")
    store.join_room("dev-b", "Bob", room.room_id)
    with pytest.raises(SignalingError) as exc:
        store.join_room("dev-c", "Carol", room.room_id)
    assert exc.value.code == ROOM_FULL


def test_orphan_controller_blocks_extra_join(store: RoomStore) -> None:
    room = store.create_room("dev-a", "Alice")
    store.join_room("dev-b", "Bob", room.room_id)
    store.leave_room("dev-a")
    with pytest.raises(SignalingError) as exc:
        store.join_room("dev-c", "Carol", room.room_id)
    assert exc.value.code == ROOM_FULL


def test_room_full(store: RoomStore) -> None:
    room = store.create_room("dev-a", "Alice")
    store.join_room("dev-b", "Bob", room.room_id)
    with pytest.raises(SignalingError) as exc:
        store.join_room("dev-c", "Carol", room.room_id)
    assert exc.value.code == ROOM_FULL


def test_grant_revoke_control(store: RoomStore) -> None:
    room = store.create_room("dev-a", "Alice")
    store.join_room("dev-b", "Bob", room.room_id, role_hint="controller")

    updated = store.grant_control("dev-a", "dev-b")
    assert updated.control_state == ControlState.CONTROL_GRANTED

    updated = store.revoke_control("dev-a", "dev-b")
    assert updated.control_state == ControlState.VIEW_ONLY


def test_non_host_cannot_grant(store: RoomStore) -> None:
    room = store.create_room("dev-a", "Alice")
    store.join_room("dev-b", "Bob", room.room_id)
    with pytest.raises(SignalingError) as exc:
        store.grant_control("dev-b", "dev-a")
    assert exc.value.code == FORBIDDEN


def test_host_leave_clears_host_until_rejoin(store: RoomStore) -> None:
    room = store.create_room("dev-a", "Alice")
    store.join_room("dev-b", "Bob", room.room_id)
    store.leave_room("dev-a")
    room = store.get_room(room.room_id)
    assert room is not None
    assert room.host_device_id is None
    assert room.members["dev-b"].role == Role.CONTROLLER

    store.leave_room("dev-b")
    _joined, member = store.join_room("dev-b", "Bob", room.room_id)
    assert member.role == Role.HOST
    assert store.get_room(room.room_id).host_device_id == "dev-b"


def test_dormant_room_rejoin_as_host(store: RoomStore) -> None:
    room = store.create_room("dev-a", "Alice")
    store.leave_room("dev-a")
    assert store.get_room(room.room_id) is not None
    assert len(store.get_room(room.room_id).members) == 0

    joined, member = store.join_room("dev-b", "Bob", room.room_id)
    assert member.role == Role.HOST
    assert joined.host_device_id == "dev-b"


def test_promote_host(store: RoomStore) -> None:
    room = store.create_room("dev-a", "Alice")
    store.join_room("dev-b", "Bob", room.room_id)
    updated = store.promote_host("dev-a", "dev-b")
    assert updated.host_device_id == "dev-b"
    assert updated.members["dev-b"].role == Role.HOST
    assert updated.members["dev-a"].role == Role.CONTROLLER


def test_pin_hash_roundtrip() -> None:
    digest = hash_pin("1234")
    assert verify_pin("1234", digest)
    assert not verify_pin("wrong", digest)


def test_unknown_room(store: RoomStore) -> None:
    with pytest.raises(SignalingError) as exc:
        store.join_room("dev-b", "Bob", "NOPE99")
    assert exc.value.code == ROOM_NOT_FOUND
