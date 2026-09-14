"""Live Avatar tests and engine-owned comparison screenshots for the ORA masters."""
import argparse
import json
from pathlib import Path
import socket
import time
import numpy as np
from PIL import Image

ROOT=Path(__file__).resolve().parent


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--port',type=int,default=19385)
    args=parser.parse_args()
    out=ROOT/'verification-master';out.mkdir(exist_ok=True)
    sock=socket.create_connection(('127.0.0.1',args.port),5)
    sock.settimeout(45); stream=sock.makefile('rwb'); sequence=0
    def call(name,arguments):
        nonlocal sequence
        sequence+=1
        stream.write((json.dumps({'jsonrpc':'2.0','id':sequence,'method':'tools/call',
            'params':{'name':name,'arguments':arguments}})+'\n').encode());stream.flush()
        r=json.loads(stream.readline())['result']
        if r.get('isError'):raise RuntimeError(r)
        return r
    def evaluate(expression):
        r=json.loads(call('eve_eval',{'expression':expression})['content'][0]['text'])
        if not r.get('ok') or r.get('type')=='error':raise RuntimeError(r)
        return r['value']
    def capture(name):
        time.sleep(.1)
        path=out/(name+'.png')
        for attempt in range(2):
            try:
                call('eve_screenshot',{'path':str(path)})
                return path
            except RuntimeError as error:
                if attempt or 'readback was just enabled' not in str(error):raise
                time.sleep(.2)
    report={'schema':'azure.live-master-verification','version':1,'engine':'Existing Workspace/Agents Windows Debug binary',
            'runtimeRebuiltFromThisWorktree':False,'comparisons':[]}
    count=evaluate('''(function(){local n=0;atelier.qa=true;
for(local p=0;p<2;++p)for(local e=0;e<6;++e)for(local h=0;h<3;++h)
for(local s=0;s<2;++s)for(local j=0;j<3;++j)for(local k=0;k<2;++k)for(local a=0;a<2;++a){
atelier.pose=p;atelier.expression=e;atelier.hair=h;atelier.shirt=s;atelier.jacket=j;
atelier.skirt=k;atelier.accessory=a==1;applyMaster();avatar.sync();
if(atelier.avatars[p].getLayerCount()!=41)throw "layer count";
if(atelier.avatars[p].getExpression()!=azureExpressions[e])throw "expression state";
n++;}return n;})()''')
    if str(count)!='864':raise RuntimeError('Unexpected state count: '+str(count))
    report['stateCombinationsPassed']=864
    for pose in range(2):
        for look in range(2):
            evaluate(f'(function(){{atelier.pose={pose};chooseMasterLook({look});atelier.reference=false;applyMaster();return "ready";}})()')
            layered=capture(f'pose-{pose}-look-{look}-layers')
            evaluate('(function(){atelier.reference=true;applyMaster();return "reference";})()')
            flat=capture(f'pose-{pose}-look-{look}-reference')
            # Exclude debug panels; compare identical transforms in the actual game.
            bounds=(430,34,830,834)
            a=np.asarray(Image.open(layered).convert('RGB').crop(bounds),dtype=int)
            b=np.asarray(Image.open(flat).convert('RGB').crop(bounds),dtype=int)
            d=np.abs(a-b)
            metrics={'pose':pose,'look':look,'meanChannelError':float(d.mean()),
                     'maxChannelError':int(d.max()),'pixelFractionOver8':float((d.max(axis=2)>8).mean())}
            report['comparisons'].append(metrics)
            Image.fromarray(np.clip(d*8,0,255).astype('uint8')).save(out/f'pose-{pose}-look-{look}-difference-x8.png')
        evaluate(f'(function(){{atelier.pose={pose};atelier.reference=false;atelier.baseOnly=true;applyMaster();return "base";}})()')
        capture(f'pose-{pose}-complete-body')
        for expression in range(6):
            evaluate(f'(function(){{chooseMasterLook({pose});atelier.expression={expression};applyMaster();return "face";}})()')
            capture(f'pose-{pose}-expression-{expression}')
    evaluate('(function(){atelier.pose=0;chooseMasterLook(0);atelier.qa=false;applyMaster();return "restored";})()')
    capture('atelier')
    report['renderComparisonPassed']=all(m['meanChannelError']<1.0 and m['pixelFractionOver8']<.02 for m in report['comparisons'])
    report['tolerance']='At display scale 0.52, mean RGB error < 1/255 and pixels >8/255 error <2%; per-layer filtering differs at alpha edges.'
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2))
    if not report['renderComparisonPassed']:raise RuntimeError('Rendered comparison exceeded tolerance')


if __name__=='__main__':main()
