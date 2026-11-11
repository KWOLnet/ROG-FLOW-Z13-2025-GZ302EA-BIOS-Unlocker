"""Exercise the compiled patcher and EFI entry point with mock UEFI protocols.
No host firmware protocols or hardware registers are accessed by these tests.
"""
from pathlib import Path
import ctypes as C
import hashlib
import itertools
import json
import struct
import subprocess
import uuid

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
manifest = json.loads((HERE / 'patch-manifest.json').read_text())
original_module = (ROOT / 'AodDxe.bin').read_bytes()
original = original_module[manifest['forms_offset']:manifest['forms_offset'] + manifest['forms_size']]
expected = (HERE / 'Aod.forms.menu-unlocked.bin').read_bytes()
dll = C.WinDLL(str(HERE / 'menu_probe_test.dll'))
dll.patch_forms.argtypes = [C.c_void_p, C.c_uint64]
dll.patch_forms.restype = C.c_int
dll.efi_main.argtypes = [C.c_void_p, C.c_void_p]
dll.efi_main.restype = C.c_uint64
checks = []


def check(name, ok):
    assert ok, name
    checks.append(name)


def invoke(data):
    b = C.create_string_buffer(data, len(data))
    rc = dll.patch_forms(b, len(data))
    return rc, b.raw


rc, actual = invoke(original)
check('compiled C patcher matches independently generated Python patch', rc == 7 and actual == expected)
rc, data = invoke(expected)
check('already patched input is rejected without modification', rc < 0 and data == expected)
damaged = bytearray(original)
damaged[manifest['edits'][0]['forms_offset'] + 4] ^= 1
rc, data = invoke(bytes(damaged))
check('missing anchor is rejected transactionally', rc < 0 and data == damaged)
damaged = bytearray(original)
damaged[5] = 1
rc, data = invoke(bytes(damaged))
check('malformed opcode is rejected transactionally', rc < 0 and data == damaged)
rc, data = invoke(original[:-1])
check('truncated package is rejected transactionally', rc < 0 and data == original[:-1])


def evaluate(b, values):
    stack = []
    p = 0
    while p < len(b):
        op, n = b[p], b[p+1] & 127
        if op == 0x12:
            q, value = struct.unpack_from('<HH', b, p+2)
            stack.append(values.get(q, 0) == value)
        elif op == 0x46:
            stack.append(True)
        elif op == 0x47:
            stack.append(False)
        elif op == 0x17:
            stack[-1] = not stack[-1]
        elif op in (0x15, 0x16):
            right, left = stack.pop(), stack.pop()
            stack.append(left and right if op == 0x15 else left or right)
        elif op == 0x29:
            break
        else:
            raise AssertionError(hex(op))
        p += n
    assert len(stack) == 1
    return stack[0]


for edit in manifest['edits']:
    old, new = bytes.fromhex(edit['original']), bytes.fromhex(edit['replacement'])
    # The expression ends at its first scope END opcode. Target question follows.
    def expression(prefix):
        p = 2
        while prefix[p] != 0x29:
            p += prefix[p+1] & 127
        return prefix[2:p+2]
    for cap, pbo in itertools.product((0, 1), (0, 1, 2, 255)):
        result = evaluate(expression(new), {0xb4: cap, 0x5c: pbo})
        assert result == (pbo != 2 if edit['question_id'] in (108, 109) else False)
check('IFR expression evaluation preserves PBO Advanced gating for both curve menus', True)
check('patched module size and all bytes outside forms are unchanged',
      len(original_module) == len((HERE / 'AodDxe.MENU_ONLY_NOT_FLASH_IMAGE.bin').read_bytes()) and
      (HERE / 'AodDxe.MENU_ONLY_NOT_FLASH_IMAGE.bin').read_bytes() ==
      original_module[:manifest['forms_offset']] + expected + original_module[manifest['forms_offset']+len(original):])

U64 = C.c_uint64
PTR = C.c_void_p
ERR = 0x8000000000000000
CALLBACK = C.WINFUNCTYPE
AOD = uuid.UUID('E39AC631-48ED-445E-9042-55B34F405844').bytes_le
HII = uuid.UUID('EF9FC172-A1B2-4693-B327-6D32FC416042').bytes_le
BROWSER = uuid.UUID('B9D4C360-BCFB-4F9B-9298-53C136982258').bytes_le
SMU = uuid.UUID('264272B3-81D1-453A-8503-8989F4382E68').bytes_le
SMBIOS3 = uuid.UUID('F2FD1544-9794-4A2C-992E-E5BBCF20E394').bytes_le
CONFIG_ACCESS = uuid.UUID('330D4706-F2A0-4E4F-A369-B66FA8D54385').bytes_le
AOD_VARIABLE = uuid.UUID('5ED15DC0-EDEF-4161-9151-6014C4CC630C').bytes_le
ROUTING = uuid.UUID('587E72D7-CC50-4F79-8209-CA291FC1A10F').bytes_le
SETUP_GUID = uuid.UUID('EC87D643-EBA4-4BB5-A1E5-3F3E36B20DA9').bytes_le
LOCK_GUID = uuid.UUID('504AF431-3025-4D32-9FBF-E9F31855551D').bytes_le
CBS = uuid.UUID('B04535E3-3004-4946-9EB7-149428983053').bytes_le
CBS_VARIABLE = uuid.UUID('3A997502-647A-4C82-998E-52EF9486A247').bytes_le
cbs_module = (ROOT / 'CbsSetupDxeSTXH.bin').read_bytes()
cbs_candidates = []
for off in range(len(cbs_module)-26):
    size = int.from_bytes(cbs_module[off:off+3], 'little')
    if cbs_module[off+3] == 2 and cbs_module[off+4] == 14 and cbs_module[off+6:off+22] == CBS and off+size <= len(cbs_module):
        cbs_candidates.append(cbs_module[off:off+size])
assert len(cbs_candidates) == 1
cbs_forms = cbs_candidates[0]


def mock_run(mode='patched', failure=None, model='GZ302EA', bios='GZ302EA.314', input_keys=None, store='aod'):
    keep, pools, output = [], {}, []
    events = []
    keys = list(input_keys) if input_keys is not None else [ord('t' if store == 'cbs' else ('2' if mode == 'patched' else '1')), ord('q'), ord('q')]
    epoch = 0
    default_variable = bytearray(801)
    struct.pack_into('<H', default_variable, 0, 801)
    default_variable[0x95] = 255
    variables = {'AodSetupStxh': bytes(default_variable)}
    if store == 'cbs':
        default_variable = bytearray(1522)
        struct.pack_into('<I', default_variable, 0, 0x12345678)
        variables['AmdSetup'] = bytes(default_variable)
    saved_variable = bytearray(default_variable)
    if store == 'cbs':
        saved_variable[0x152] = 1
        struct.pack_into('<I', saved_variable, 0x153, 95)
    else:
        saved_variable[0x95] = 2
        saved_variable[0xbc] = 1
        saved_variable[0xbd] = 1
        struct.pack_into('<H', saved_variable, 0xbe, 10)
    repair_case = bool(failure and failure.startswith('fix_'))
    locked = True
    if failure == 'fix_other_settings':
        struct.pack_into('<I', saved_variable, 0x2c, 4200)
        struct.pack_into('<H', saved_variable, 0x30, 1200)
        struct.pack_into('<H', saved_variable, 0x34, 2500)
        struct.pack_into('<H', saved_variable, 0x36, 1000)
    if failure == 'fix_metadata':
        saved_variable[2] ^= 1
    if failure == 'fix_no_change':
        saved_variable[:] = default_variable
    if failure in ('wrong_model', 'wrong_version', 'anchor'):
        keys.insert(0, ord('q'))
    forms = bytearray(original)
    if failure == 'anchor':
        forms[manifest['edits'][0]['forms_offset'] + 4] ^= 1
    current = bytes(forms)
    list_guid = uuid.UUID('442BA91E-B0A8-499F-94F7-2E922C9AAE0D').bytes_le

    def hold(obj):
        keep.append(obj)
        return C.addressof(obj)

    def cb(restype, args, fn):
        obj = CALLBACK(restype, *args)(fn)
        keep.append(obj)
        return C.cast(obj, PTR).value

    def wr64(p, v):
        C.cast(p, C.POINTER(U64))[0] = v

    def allocate(kind, size, out):
        b = C.create_string_buffer(size)
        addr = C.addressof(b)
        pools[addr] = b
        wr64(out, addr)
        return 0

    def free(addr):
        pools.pop(addr)
        return 0

    def put(this, text):
        output.append(C.wstring_at(text))
        return 0

    def read(this, key):
        value = keys.pop(0) if keys else ord('q')
        scan, unicode = value if isinstance(value, tuple) else (0, value)
        C.memmove(key, struct.pack('<HH', scan, unicode), 4)
        return 0

    def lists(this, kind, guid, size, buf):
        events.append('list')
        wr64(size, 16)
        if not buf:
            return ERR | 5
        wr64(buf, 0x1234+epoch)
        wr64(buf+8, 0x2234)
        return 0

    def export(this, handle, size, buf):
        nonlocal current
        assert handle in (0x1234+epoch, 0x2234), 'stale HII handle used for export'
        exported_forms = cbs_forms if handle == 0x2234 else current
        packet = list_guid + struct.pack('<I', 24+len(exported_forms)) + exported_forms + b'\x04\0\0\xdf'
        wr64(size, len(packet))
        if not buf:
            return ERR | 5
        C.memmove(buf, packet, len(packet))
        return 0

    def update(this, handle, packet):
        nonlocal current, epoch
        assert handle == 0x1234+epoch, 'stale HII handle used for update'
        n = C.c_uint32.from_address(packet+16).value
        b = C.string_at(packet, n)
        assert b[-4:] == b'\x04\0\0\xdf'
        current = b[20:-4]
        events.append('update' if current == expected else 'restore')
        if failure == 'partial_update' and events[-1] == 'update':
            return ERR | 7
        if failure == 'restore' and events[-1] == 'restore':
            return ERR | 7
        if events[-1] == 'restore':
            epoch += 1
            if failure == 'submit_clear_on_restore':
                variables['AodSetupStxh'] = bytes(default_variable)
        return 0

    def send(this, handles, count, guid, form, dim, action):
        assert count == 1
        is_cbs = C.string_at(guid, 16) == CBS
        if is_cbs:
            assert form in (0x7006, 0x7000) and C.c_uint64.from_address(handles).value == 0x2234
            events.append('cbs_thermal' if form == 0x7006 else 'cbs_root')
        else:
            assert C.string_at(guid, 16) == AOD and form == 0x98
            assert C.c_uint64.from_address(handles).value == 0x1234+epoch
            assert current == (expected if mode == 'patched' else original)
        events.append('browser')
        if failure in ('submit_saved', 'submit_rejected', 'submit_clear_on_restore') or repair_case:
            route = CALLBACK(U64, PTR, PTR, PTR)(config_access[1])
            configuration = C.create_unicode_buffer('GUID=mock&NAME='+('AmdSetup' if is_cbs else 'AodSetupStxh')+'&OFFSET=0&WIDTH=mock&VALUE=mock')
            progress = PTR()
            result = route(C.addressof(config_access), C.addressof(configuration), C.addressof(progress))
            if failure == 'submit_rejected':
                assert result == ERR | 15
        wr64(action, 1 if failure == 'reset' else 0)
        return ERR | 3 if failure == 'browser' else 0

    def capabilities(this, instance, message, arg, result):
        assert message == 94 and instance == 0
        events.append('caps')
        C.cast(result, C.POINTER(C.c_uint32))[0] = 0xc6
        return 0

    db = (U64*11)()
    db[2] = cb(U64, [PTR, PTR, PTR], update)
    db[3] = cb(U64, [PTR, C.c_uint8, PTR, PTR, PTR], lists)
    db[4] = cb(U64, [PTR, PTR, PTR, PTR], export)
    def package_driver(this, handle, driver):
        assert handle in (0x1234+epoch, 0x2234)
        wr64(driver, 0xbeef)
        return 0
    db[10] = cb(U64, [PTR, PTR, PTR], package_driver)
    def native_route(this, configuration, progress):
        assert this == C.addressof(config_access)
        native_name = 'AmdSetup' if 'NAME=AmdSetup&' in C.wstring_at(configuration) else 'AodSetupStxh'
        assert 'NAME='+native_name in C.wstring_at(configuration)
        events.append('route')
        if failure == 'submit_rejected':
            return ERR | 15
        if repair_case:
            if failure == 'fix_partial_failure':
                variables[native_name] = bytes(saved_variable)
                return ERR | 15
            if failure == 'fix_still_denied' or (locked and failure != 'fix_native_success'):
                return ERR | 15
        variables[native_name] = bytes(saved_variable)
        if native_name == 'AodSetupStxh' and failure != 'fix_last_missing':
            variables['AodSetupStxhLast'] = bytes(saved_variable)
        return 0
    config_access = (U64*3)()
    native_route_pointer = cb(U64, [PTR, PTR, PTR], native_route)
    config_access[1] = native_route_pointer
    if repair_case:
        def native_extract(this, request, progress, result):
            assert this == C.addressof(config_access) and request is None
            events.append('extract')
            buffer = C.create_string_buffer(b'mock config\0')
            addr = C.addressof(buffer)
            pools[addr] = buffer
            wr64(result, addr)
            return ERR | 7 if failure == 'fix_extract_error' else 0
        config_access[0] = cb(U64, [PTR, PTR, PTR, PTR], native_extract)
    fb = (U64*2)()
    fb[0] = cb(U64, [PTR, PTR, U64, PTR, C.c_uint16, PTR, PTR], send)
    smu = (U64*4)()
    smu[3] = cb(U64, [PTR, U64, C.c_uint32, PTR, PTR], capabilities)
    protocol_map = {HII: hold(db), BROWSER: hold(fb), SMU: hold(smu)}
    if repair_case:
        routing = (U64*6)()
        def config_to_block(this, config, block, size, progress):
            events.append('convert')
            assert C.c_uint64.from_address(size).value == len(saved_variable)
            if failure == 'fix_bad_configuration':
                return ERR | 2
            C.memmove(block, bytes(saved_variable), len(saved_variable))
            wr64(size, len(saved_variable))
            return 0
        routing[4] = cb(U64, [PTR, PTR, PTR, PTR, PTR], config_to_block)
        protocol_map[ROUTING] = hold(routing)

    def locate(guid, registration, out):
        p = protocol_map.get(C.string_at(guid, 16))
        wr64(out, p or 0)
        return 0 if p else ERR | 14

    def watchdog(timeout, code, size, data):
        assert (timeout, code, size, data) == (0, 0, 0, None)
        events.append('watchdog_disabled')
        return 0
    def handle_protocol(handle, guid, output):
        assert handle == 0xbeef and C.string_at(guid, 16) == CONFIG_ACCESS
        wr64(output, C.addressof(config_access))
        return 0
    def get_variable(name, guid, attributes, size, data):
        requested_guid = C.string_at(guid, 16)
        if requested_guid == SETUP_GUID:
            assert C.wstring_at(name) == 'Setup'
            setup = bytearray(148)
            setup[0x50] = 0 if failure == 'fix_protection_disabled' else 1
            value = bytes(setup)
        else:
            assert requested_guid in (AOD_VARIABLE, CBS_VARIABLE)
            value = variables.get(C.wstring_at(name))
        if value is None:
            return ERR | 14
        available = C.c_uint64.from_address(size).value
        wr64(size, len(value))
        if available < len(value):
            return ERR | 5
        C.c_uint32.from_address(attributes).value = 7
        C.memmove(data, value, len(value))
        return 0
    def set_mailbox(name, guid, attributes, size, data):
        nonlocal locked
        # No direct write of AOD, Setup, passwords or nonvolatile protection flags.
        assert C.wstring_at(name) == 'NvLockMailbox'
        assert C.string_at(guid, 16) == LOCK_GUID
        assert attributes == 2 and size == 6
        command = C.string_at(data, size)
        assert command[:4] == b'ANV$' and command[5] == 0 and command[4] in (0, 1)
        if command[4] == 0:
            events.append('unlock')
            locked = False
            return ERR | 8 if failure == 'fix_unlock_error' else 0
        events.append('relock')
        if failure == 'fix_relock_error':
            return ERR | 15
        locked = True
        return 0

    boot = C.create_string_buffer(376)
    for offset, pointer in [(64, cb(U64, [C.c_uint32, U64, PTR], allocate)),
                            (72, cb(U64, [PTR], free)),
                            (152, cb(U64, [PTR, PTR, PTR], handle_protocol)),
                            (256, cb(U64, [U64, U64, U64, PTR], watchdog)),
                            (320, cb(U64, [PTR, PTR, PTR], locate))]:
        C.c_uint64.from_buffer(boot, offset).value = pointer
    console = (U64*10)()
    console[1] = cb(U64, [PTR, PTR], put)
    inp = (U64*3)()
    inp[1] = cb(U64, [PTR, PTR], read)
    # SMBIOS Type 0 BIOS version and Type 1 product, followed by Type 127.
    smdata = bytes([0,6,0,0,0,1]) + bios.encode() + b'\0\0'
    smdata += bytes([1,6,1,0,0,1]) + model.encode() + b'\0\0'
    smdata += bytes([127,4,2,0,0,0])
    smtable = C.create_string_buffer(smdata)
    smheader = C.create_string_buffer(24)
    C.memmove(smheader, b'_SM3_\0\x18', 7)
    C.c_uint32.from_buffer(smheader, 12).value = len(smdata)
    C.c_uint64.from_buffer(smheader, 16).value = hold(smtable)
    config = C.create_string_buffer(24)
    C.memmove(config, SMBIOS3, 16)
    C.c_uint64.from_buffer(config, 16).value = hold(smheader)
    system = C.create_string_buffer(120)
    runtime = C.create_string_buffer(136)
    C.c_uint64.from_buffer(runtime, 72).value = cb(U64, [PTR, PTR, PTR, PTR, PTR], get_variable)
    if repair_case:
        C.c_uint64.from_buffer(runtime, 88).value = cb(U64, [PTR, PTR, C.c_uint32, U64, PTR], set_mailbox)
    for off, value in [(48, hold(inp)), (64, hold(console)), (96, hold(boot)),
                       (88, hold(runtime)), (104, 1), (112, hold(config))]:
        C.c_uint64.from_buffer(system, off).value = value
    result = dll.efi_main(0, C.addressof(system))
    assert config_access[1] == native_route_pointer, 'native RouteConfig was not restored'
    if failure != 'fix_relock_error':
        assert locked, 'native variable protection was left unlocked'
    assert not pools, 'allocated buffers were not released'
    return result, events, current, ''.join(output)


result, events, current, _ = mock_run()
check('EFI mocked end-to-end patch/browser/restore', result == 0 and events[-3:] == ['update', 'browser', 'restore'] and current == original)
result, events, current, _ = mock_run(mode='native')
check('original-menu path performs no HII update', result == 0 and 'browser' in events and 'update' not in events and 'restore' not in events)
for failure in ('partial_update', 'browser'):
    result, events, current, _ = mock_run(failure=failure)
    check(f'original forms restored after {failure} failure', events[-1] == 'restore' and current == original)
result, events, current, output = mock_run(failure='restore')
check('restore error stops further menu operations', result & ERR and events[-1] == 'restore' and 'Reboot before' in output)
for failure, kwargs in [('wrong_model', {'model': 'OTHER'}), ('wrong_version', {'bios': 'GZ302EA.313'})]:
    result, events, _, _ = mock_run(failure=failure, **kwargs)
    check(f'{failure} aborts before accessing HII or SMU', result & ERR and events == [])
result, events, _, _ = mock_run(failure='anchor')
check('runtime anchor mismatch never updates HII or opens browser', result & ERR and 'update' not in events and 'browser' not in events)
result, events, _, output = mock_run(input_keys=[ord('u'), 13, ord('q')])
check('U letter shortcut opens unlocked browser', result == 0 and 'browser' in events and 'Selection accepted' in output)
result, events, _, output = mock_run(input_keys=[(2, 0), ord('@'), ord('U'), 13, ord('q')])
check('unrecognized keys report Unicode and scan codes before U opens browser', result == 0 and events.count('browser') == 1 and output.count('Ignored key:') == 2)
result, events, _, _ = mock_run(input_keys=[0xff12, 13, ord('q')])
check('fullwidth digit two is normalized', result == 0 and 'browser' in events)
result, events, _, _ = mock_run(input_keys=[ord('u'), 13, ord('u'), 13, ord('q')])
check('repeat entry reacquires changed live HII handles', result == 0 and events.count('browser') == 2 and events.count('restore') == 2)
result, events, _, output = mock_run(failure='reset', input_keys=[ord('u'), 13, ord('u'), ord('q')])
check('firmware reset request stops further entries without issuing a reset', result == 0 and events.count('browser') == 1 and 'Restart before opening' in output)
result, events, _, output = mock_run(input_keys=[ord('u'), 13, ord('q')])
check('C6 capability result reports manual voltage unavailable and curve advertised',
      'CPU manual voltage: NOT advertised' in output and 'Curve menu capability: advertised' in output)
result, events, _, output = mock_run(failure='submit_saved')
check('save observer delegates native submission and reads changed nonvolatile PBO/Curve',
      result == 0 and events.count('route') == 1 and 'Native RouteConfig calls: 1' in output and
      'PBO/Curve readback changed across browser.' in output and 'NV=yes PBO=2 CPU-mode=1 CPU=-10' in output)
result, events, _, output = mock_run(failure='submit_rejected')
check('save observer reports native access-denied without fallback writes',
      result == 0 and events.count('route') == 1 and 'Final RouteConfig result: 0x800000000000000F' in output and
      'PBO/Curve readback unchanged across browser.' in output)
result, events, _, output = mock_run(failure='submit_clear_on_restore')
check('save observer detects settings overwritten during HII restore',
      result == 0 and 'Configuration readback changed during forms restore.' in output)
result, events, _, output = mock_run()
check('view-only browser reports no submission while restoring original callback',
      result == 0 and 'route' not in events and 'Native RouteConfig calls: 0' in output)
result, events, _, output = mock_run(input_keys=[ord('r'), ord('q')])
check('R key reads stored variables without opening browser',
      result == 0 and 'browser' not in events and 'CURRENT MAIN:' in output)
result, events, _, output = mock_run(failure='fix_saved')
check('locked native save retries in temporary context and verifies MAIN plus LAST',
      result == 0 and events.count('route') == 2 and events.count('unlock') == 1 and
      events.count('relock') == 1 and 'MAIN + LAST readback verification: 0x0000000000000000' in output)
check('native cache refresh precedes unlock and relock precedes forms restore',
      events.index('extract') < events.index('unlock') < events.index('relock') < events.index('restore'))
result, events, _, output = mock_run(failure='fix_other_settings')
check('native retry covers manual CPU/GFX fields as well as Curve',
      result == 0 and events.count('route') == 2 and 'MAIN + LAST readback verification: 0x0000000000000000' in output)
result, events, _, output = mock_run(failure='fix_native_success')
check('already-successful native submissions never change protection state',
      result == 0 and events.count('route') == 1 and 'unlock' not in events and 'relock' not in events)
for failure in ('fix_metadata', 'fix_no_change', 'fix_bad_configuration',
                'fix_protection_disabled', 'fix_partial_failure', 'fix_extract_error'):
    result, events, _, output = mock_run(failure=failure)
    check(f'{failure} skips temporary context and extra native writes',
          events.count('route') == 1 and 'unlock' not in events and 'relock' not in events)
result, events, _, output = mock_run(failure='fix_still_denied')
check('native rejection is preserved after one retry and protection is re-locked',
      events.count('route') == 2 and events.count('relock') == 1 and
      'Native save retry: 0x800000000000000F' in output)
result, events, _, output = mock_run(failure='fix_unlock_error')
check('partial unlock error still re-locks and does not retry native save',
      events.count('route') == 1 and events.count('unlock') == 1 and events.count('relock') == 1)
result, events, _, output = mock_run(failure='fix_last_missing')
check('missing LAST readback is never reported as verified success',
      'MAIN + LAST readback verification: 0x8000000000000021' in output and events.count('relock') == 1)
result, events, _, output = mock_run(store='cbs')
check('T opens the unmodified native CBS SMU Common Options form',
      result == 0 and 'cbs_thermal' in events and 'update' not in events and 'restore' not in events)
result, events, _, output = mock_run(store='cbs', input_keys=[ord('b'), 13, ord('q')])
check('B opens the full unmodified native AMD CBS form set',
      result == 0 and 'cbs_root' in events and 'update' not in events)
result, events, _, output = mock_run(store='cbs', failure='fix_cbs_saved')
check('CBS TjMax save uses its own 1522-byte namespace and native-context retry',
      result == 0 and events.count('route') == 2 and events.count('relock') == 1 and
      'CBS MAIN readback verification: 0x0000000000000000' in output and 'NV=yes ThermalControl=1 TjMax=95 C' in output)
result, events, _, output = mock_run(store='cbs', failure='fix_still_denied')
check('CBS native rejection is retained and protection is re-locked',
      events.count('route') == 2 and events.count('relock') == 1 and 'Native save retry: 0x800000000000000F' in output)
result, events, _, output = mock_run(store='cbs', failure='fix_metadata')
check('CBS storage identity changes stop retry before opening write context',
      events.count('route') == 1 and 'unlock' not in events)
result, events, _, output = mock_run(store='cbs', input_keys=[ord('t'), 13, ord('u'), 13, ord('q')])
check('switching from CBS back to AOD restores the correct form and storage context',
      result == 0 and events.count('browser') == 2 and events.count('update') == 1 and events.count('restore') == 1)
# Run permanent mock re-lock failure last: each real EFI image starts with fresh
# globals; this DLL deliberately retains need_relock to exercise all cleanup retries.
result, events, _, output = mock_run(failure='fix_relock_error', input_keys=[ord('u'), 13, ord('u'), ord('q')])
check('re-lock failure stops before HII restore or another browser entry',
      result & ERR and events.count('browser') == 1 and 'restore' not in events and
      events.count('relock') >= 2 and 'Restart the computer now' in output)

efi = (HERE / 'GZ302EA314_MENU_PROBE.efi').read_bytes()
pe = struct.unpack_from('<I', efi, 0x3c)[0]
opt = pe+24
check('EFI binary is x64 PE32+ EFI application', efi[pe:pe+4] == b'PE\0\0' and struct.unpack_from('<H', efi, pe+4)[0] == 0x8664 and struct.unpack_from('<H', efi, opt)[0] == 0x20b and struct.unpack_from('<H', efi, opt+68)[0] == 10)
directories = opt+112
check('EFI binary has relocations and no OS imports',
      struct.unpack_from('<II', efi, directories+8) == (0, 0) and struct.unpack_from('<II', efi, directories+40)[1] > 0)
subprocess.run([str(ROOT / 'tools/ifr/ifrextractor.exe'),
                str(HERE / 'AodDxe.MENU_ONLY_NOT_FLASH_IMAGE.bin')], check=True, capture_output=True)
stock = (ROOT / 'AodDxe.bin.0.0.en-US.uefi.ifr.txt').read_text('utf-8-sig')
unlocked = (HERE / 'AodDxe.MENU_ONLY_NOT_FLASH_IMAGE.bin.0.0.en-US.uefi.ifr.txt').read_text('utf-8-sig')
def definitions(text):
    return [line.strip() for line in text.splitlines()
            if any(marker in line for marker in ('Prompt:', 'OneOfOption', 'Default ', 'VarStore ', 'Form FormId'))]
check('Independent IFRExtractor reparse: all forms, controls, defaults and ranges preserved', definitions(stock) == definitions(unlocked))
result = {'passed': len(checks), 'checks': checks,
          'efi_sha256': hashlib.sha256(efi).hexdigest(),
          'target_hardware_boot_tested': False, 'voltage_effect_tested': False,
          'test_environment': 'Windows DLL, mock UEFI protocols only; no host firmware access'}
(HERE / 'VERIFICATION.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps(result, indent=2))
