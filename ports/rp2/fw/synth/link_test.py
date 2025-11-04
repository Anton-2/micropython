import wave
import random

voices = [wave.Voice() for _ in range(10)]


def vname(voice):
    for idx, v in enumerate(voices):
        if v == voice:
            return idx
    return None


head = voices[0]
for v in voices[1:]:
    v.link(head)

in_ring = voices[:]
free_ring = None

while in_ring:
    to_remove = random.choice(in_ring)
    print(f"head={vname(head)}, remove {vname(to_remove)}")
    next = to_remove.unlink()
    if head is to_remove:
        head = next
    in_ring.remove(to_remove)

    if free_ring:
        to_remove.link(free_ring)
    else:
        free_ring = to_remove

print(f"head is {vname(head)}")

while free_ring:
    print(f"free unlink {vname(free_ring)}")
    free_ring = free_ring.unlink()
