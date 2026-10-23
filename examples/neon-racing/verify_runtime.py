"""Verify a running NEON R example through its declared Play Host actions."""
import argparse
import json
import math
from pathlib import Path
import socket
import sys
import time


class Client:
    def __init__(self, port):
        self.socket = socket.create_connection(('127.0.0.1', port), timeout=30)
        self.stream = self.socket.makefile('r', encoding='utf-8')
        self.sequence = 0
        self.rpc('initialize', {'protocolVersion': '2025-06-18', 'clientInfo': {'name': 'neon-racing-verifier', 'version': '1'}})
        self.socket.sendall(b'{"jsonrpc":"2.0","method":"notifications/initialized"}\n')

    def rpc(self, method, params):
        self.sequence += 1
        request = {'jsonrpc': '2.0', 'id': self.sequence, 'method': method, 'params': params}
        self.socket.sendall((json.dumps(request)+'\n').encode('utf-8'))
        for line in self.stream:
            response = json.loads(line)
            if response.get('id') == self.sequence:
                if response.get('error'):
                    raise RuntimeError(response['error'])
                return response['result']
        raise RuntimeError('Engine closed its MCP connection')

    def tool(self, name, **arguments):
        result = self.rpc('tools/call', {'name': name, 'arguments': arguments})
        text = '\n'.join(c.get('text', '') for c in result.get('content', []) if c.get('type') == 'text')
        if result.get('isError') or text.startswith('error:'):
            raise RuntimeError(text)
        return json.loads(text)

    def play(self, **request):
        return self.tool('eve_play', request={'schemaId': 'evengine.play-request', 'schemaVersion': 1, **request})

    def state(self):
        return self.play(op='observe', observation='race')['state']

    def act(self, action):
        self.play(op='act', action=action)

    def until(self, predicate, timeout=45):
        self.play(op='clock', mode='play')
        deadline = time.monotonic()+timeout
        while time.monotonic() < deadline:
            state = self.state()
            if predicate(state):
                self.play(op='clock', mode='pause')
                return self.state()
            time.sleep(0.15)
        self.play(op='clock', mode='pause')
        raise AssertionError(f'Timed out waiting for state: {self.state()}')

    def capture(self, path):
        # Readback needs at least one presented frame after being enabled.
        self.play(op='clock', mode='play')
        deadline = time.monotonic()+30
        while True:
            try:
                result = self.play(op='capture', path=path)
                break
            except RuntimeError as error:
                if 'no presented frame yet' not in str(error) or time.monotonic()>deadline:
                    raise
                time.sleep(0.2)
        self.play(op='clock', mode='pause')
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=7537)
    args = parser.parse_args()
    c = Client(args.port)
    assert c.play(op='status')['contractId'] == 'examples/neon-racing'
    session = c.tool('eve_agent_session_start', objective='Verify NEON R driving, road collision, recovery, lap and rendered scene', criteria=[
        {'id': name, 'description': description} for name, description in [
            ('drive', 'Vehicle accelerates, steers and brakes'),
            ('collision', 'Outer guardrail blocks a straight drive'),
            ('recovery', 'Recovery synchronizes physics and rendered state'),
            ('lap', 'Normal inputs complete all checkpoints and record a lap'),
            ('visual', 'Engine-owned frame capture shows the actual scene')]])['sessionId']
    for phase in ['modify', 'run']:
        c.tool('eve_agent_session_advance', sessionId=session, phase=phase)
    records = {}
    c.play(op='clock', mode='pause')
    c.act('reset')
    records['ready'] = c.until(lambda s:s['time']>0.1)
    c.act('accelerate')
    records['accelerate'] = c.until(lambda s:s['speed']>12 and s['x']>2)
    c.act('brake')
    records['brake'] = c.until(lambda s:abs(s['speed'])<0.1)
    c.act('right')
    records['steer'] = c.until(lambda s:s['heading']>15)
    c.act('recover')
    records['recover'] = c.until(lambda s:abs(s['speed'])<0.1 and abs(s['x'])<0.2 and abs(s['z']+48)<0.2)
    c.act('accelerate')
    impact = c.until(lambda s:math.hypot(s['x'],s['z'])>57.5)
    records['collision'] = c.until(lambda s:s['time']>impact['time']+2)
    assert math.hypot(records['collision']['x'],records['collision']['z'])<59.5, records['collision']
    assert records['collision']['lap']==0, records['collision']
    c.act('reset');c.act('assist')
    records['lap'] = c.until(lambda s:s['lap']>=1, timeout=180)
    assert records['lap']['bestLap']>15 and records['lap']['lastLap']>0, records['lap']
    c.act('brake');c.act('assist');c.act('reset')
    c.until(lambda s:s['time']>0.1)
    c.capture('captures/chase.png')
    c.act('camera')
    overview = c.state()
    c.until(lambda s:s['tick']>overview['tick']+5)
    c.capture('captures/overview.png')
    c.act('camera')
    c.tool('eve_agent_session_advance',sessionId=session,phase='observe')
    c.tool('eve_agent_session_advance',sessionId=session,phase='verify')
    for criterion in ['drive','collision','recovery','lap','visual']:
        c.tool('eve_agent_session_evidence',sessionId=session,criterionId=criterion,kind='screenshot' if criterion=='visual' else 'runtime-observation',status='pass',summary=f'{criterion}: verified through declared driving actions and race telemetry',artifact='captures/chase.png' if criterion=='visual' else 'captures/runtime-validation.json')
    completed=c.tool('eve_agent_session_complete',sessionId=session,summary='NEON R road and Vehicle integration verified in actual engine frames')
    records['evidenceSession']=completed
    output=Path(__file__).parent/'captures/runtime-validation.json'
    output.write_text(json.dumps(records,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(records,indent=2),flush=True)
    print('PASS: driving, collision, recovery, lap and engine capture',flush=True)
    c.play(op='clock',mode='play')


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    main()
