"""Exercise real MCP clients racing against one live whitebox editor."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import threading
import uuid

from agent_demo import WhiteboxClient


def check(port):
    client = WhiteboxClient(port)
    prefix = "tx-" + uuid.uuid4().hex[:8]

    def submit(name, operations, revision=None):
        if revision is None:
            revision = client.call("whitebox_scene_v1")["revision"]
        request = dict(
            transactionId=prefix + name,
            expectedRevision=revision,
            operations=operations,
        )
        return request, client.call("whitebox_transaction_v1", request, allow_error=True)

    def create(name, x):
        return dict(type="create", id=prefix + name, recipe="prototype.cube", position=[x, 0, 0])

    request, result = submit("initial", [create("a", -3), create("b", 3)])
    assert result["ok"], result
    after = client.call("whitebox_scene_v1")
    replay = client.call("whitebox_transaction_v1", request)
    assert replay["value"] == result["value"]
    assert client.call("whitebox_scene_v1") == after
    changed = dict(request, operations=[create("different", 4)])
    assert (
        client.call("whitebox_transaction_v1", changed, allow_error=True)["code"]
        == "transaction_id_conflict"
    )
    assert client.call("whitebox_scene_v1") == after

    exact, committed = submit("exact", [dict(create("exact", 9), name="A\x01B")])
    assert committed["ok"], committed
    before = client.call("whitebox_scene_v1")
    changed = dict(exact, operations=[dict(exact["operations"][0], name="A B")])
    assert (
        client.call("whitebox_transaction_v1", changed, allow_error=True)["code"]
        == "transaction_id_conflict"
    )
    assert client.call("whitebox_scene_v1") == before

    for invalid in [
        dict(transactionId=prefix + "empty", expectedRevision=before["revision"], operations=[]),
        dict(
            transactionId=prefix + "unknown",
            expectedRevision=before["revision"],
            operations=[create("unknown", 0)],
            extra=True,
        ),
    ]:
        response = client.call("whitebox_transaction_v1", invalid, allow_error=True)
        assert not response["ok"], response
        assert client.call("whitebox_scene_v1") == before

    for index, bad in enumerate(
        [
            dict(type="create", id=prefix + "bad", recipe="missing", position=[0, 0, 0]),
            dict(type="transform", id=prefix + "a", scale=[1, 0, 1]),
            dict(type="delete", id=prefix + "missing"),
        ]
    ):
        before = client.call("whitebox_scene_v1")
        _, rejected = submit("invalid" + str(index), [create("unpublished", 0), bad])
        assert not rejected["ok"], rejected
        assert client.call("whitebox_scene_v1") == before

    # A failed transaction must not reserve its ID or leave its first operation behind.
    _, retry = submit("invalid0", [create("unpublished", 0)])
    assert retry["ok"], retry

    barrier = threading.Barrier(2)
    revision = client.call("whitebox_scene_v1")["revision"]

    def race(name, x):
        other = WhiteboxClient(port)
        body = dict(
            transactionId=prefix + name, expectedRevision=revision, operations=[create(name, x)]
        )
        barrier.wait(timeout=10)
        return body, other.call("whitebox_transaction_v1", body, allow_error=True)

    with ThreadPoolExecutor(max_workers=2) as executor:
        futures = [executor.submit(race, "agent-a", -6), executor.submit(race, "agent-b", 6)]
        results = [f.result(timeout=90) for f in futures]
    assert sum(r[1]["ok"] for r in results) == 1, results
    loser, failure = next(r for r in results if not r[1]["ok"])
    assert failure["code"] == "stale_revision", failure
    raced = client.call("whitebox_scene_v1")
    raced_ids = {o["id"] for o in raced["value"]["objects"]}
    assert loser["operations"][0]["id"] not in raced_ids
    winner = next(body for body, response in results if response["ok"])
    assert winner["operations"][0]["id"] in raced_ids
    loser["expectedRevision"] = raced["revision"]
    assert client.call("whitebox_transaction_v1", loser)["ok"]

    _, mixed = submit(
        "mixed",
        [
            dict(type="transform", id=prefix + "a", position=[-3, 1, 0]),
            dict(type="delete", id=prefix + "b"),
            create("c", 3),
        ],
    )
    assert mixed["ok"], mixed
    objects = {o["id"]: o for o in client.call("whitebox_scene_v1")["value"]["objects"]}
    assert objects[prefix + "exact"]["name"] == "A\x01B"
    assert objects[prefix + "a"]["transform"]["y"] == 1
    assert prefix + "b" not in objects and prefix + "c" in objects
    print(
        "PASS: batch create, mixed edits, identical retry, ID mismatch, 5 failure cases, two concurrent clients and conflict retry"
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, required=True)
    check(parser.parse_args().port)
