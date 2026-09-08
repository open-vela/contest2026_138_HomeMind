# MiHome adapter staging — 2026-09-02

HomeMind now contains an optional gateway-side adapter at
`backend/gateway-agent/mihome_adapter.py`.

- It calls a user-owned Home Assistant REST API, which is the boundary exposed
  by the official Xiaomi Home Integration; Xiaomi credentials never enter the
  ESP32 firmware or the repository.
- Only `light.*` and `switch.*` entity IDs in
  `HOMEMIND_MIHOME_ALLOWED_ENTITIES` are accepted.
- `mihome.set_power` performs the service call and then reads the entity state
  back, failing if the requested `on/off` state is not confirmed.
- The adapter is disabled unless URL, bearer token, and a non-empty allowlist
  are all present. Unit tests use a fake HTTP opener and make no network call.

This is an integration-ready safety boundary, not a real MiHome device pass.
A true pass still needs a Xiaomi account/region, an actual supported device,
Home Assistant installation/configuration, and repeated on/off observations.
