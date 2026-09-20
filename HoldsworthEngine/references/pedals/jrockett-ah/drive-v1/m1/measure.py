#!/usr/bin/env python3
"""Compare actual causal C++ M1 with the unchanged approved offline sound oracle.

Usage: python measure.py /tmp/ah-drive-m1/oracle.dylib [--out /tmp/results]
Writes only one combined CSV and one JSON summary. No new audio grid retained.
"""
import argparse
import csv
import ctypes
import hashlib
import importlib.util
import json
from pathlib import Path
import numpy as np

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location("approved_oracle",HERE.parent/"prototype.py")
oracle=importlib.util.module_from_spec(spec)
spec.loader.exec_module(oracle)


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("library",type=Path)
    parser.add_argument("--out",type=Path,default=HERE)
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    lib=ctypes.CDLL(str(args.library.resolve()))
    array=np.ctypeslib.ndpointer(dtype=np.float64,ndim=1,flags="C_CONTIGUOUS")
    lib.ah_drive_render.argtypes=[ctypes.c_double]*5+[array,array,ctypes.c_size_t,ctypes.c_size_t]
    lib.ah_drive_render.restype=ctypes.c_int
    def native(x,fs,gain=.5,bass=.5,treble=.5,volume=10**(-.2),block=127):
        data=np.ascontiguousarray(np.pad(x,(0,32)),dtype=np.float64)
        y=np.zeros_like(data)
        assert lib.ah_drive_render(fs,gain,bass,treble,volume,data,y,len(data),block)==32
        return y[32:32+len(x)]
    rows=[];worst_abs=0.;worst_relative=0.;worst=None
    for fs in oracle.P["supported_rates_hz"]:
        for gain in (0.,.5,1.):
            for hz,amp in ((110,.02),(440,.05),(440,.2),(110,.5),(1000,.5),
                           (2000,.5),(4000,.5),(6000,.05),(8000,.05)):
                for bass,treble in ((.5,.5),(0.,0.),(0.,1.),(1.,0.),(1.,1.)):
                    x,k=oracle.tone(fs,hz,amp);n=len(x)
                    opts=dict(gain=gain,bass=bass,treble=treble)
                    y=native(np.tile(x,3),fs,**opts)[n:2*n]
                    r4=oracle.drive(x,fs,factor=4,periodic=True,**opts)
                    r32=oracle.drive(x,fs,factor=32,periodic=True,**opts)
                    error=float(np.max(abs(y-r4)))
                    relative=error/max(1e-20,float(np.max(abs(r4))))
                    resid=oracle.residual(y,r32,fs)
                    row=dict(group="fidelity",fs=fs,gain=gain,bass=bass,treble=treble,
                             input_peak=amp,hz=k*fs/n,max_oracle_error=error,
                             residual_dbr=resid)
                    rows.append(row)
                    worst_abs=max(worst_abs,error);worst_relative=max(worst_relative,relative)
                    if worst is None or resid>worst["residual_dbr"]: worst=row
        print("C++ oracle/fidelity rate",fs,flush=True)
    # Explicit Gain/input cleanup and actual nonlinear spectra at neutral tones.
    for gain in (0.,.5,1.):
        for amp in (.02,.05,.2,.5):
            x,k=oracle.tone(48000,440.,amp);n=len(x)
            y=native(np.tile(x,3),48000,gain=gain)[n:2*n]
            h,thd=oracle.harmonics(y,k)
            rows.append(dict(group="cleanup",gain=gain,input_peak=amp,thd_percent=thd,
                             fundamental=float(h[0])))
    # Independent post-Volume checks at mute, attenuated, unity and maximum.
    fs,di,source=oracle.read_di();x=np.pad(di,(128,128))
    base=oracle.drive(x,fs)
    volume_error=0.
    for v in (0.,.1,.25,.5,10**(-.2),1.):
        y=native(x,fs,volume=v)
        amplitude=10**(.6)*v**3
        error=float(np.max(abs(y-base*amplitude)))
        volume_error=max(volume_error,error)
        if v==0.: assert np.all(y==0.)
        rows.append(dict(group="volume",volume=v,amplitude=amplitude,max_oracle_error=error))
    # Retain actual guitar and all settled Boost-cascade comparisons from design.
    for gain in (0.,.5,1.):
        for peak in (.05,.2,.5):
            signal=x*peak/.2
            y=native(signal,fs,gain=gain)
            ref=oracle.drive(signal,fs,gain=gain,factor=32)
            rows.append(dict(group="di",gain=gain,input_peak=peak,residual_dbr=oracle.residual(y,ref,fs)))
    for mode in range(6):
        for level in (0.,6.,12.,20.):
            signal=oracle.boost(x*.25,fs,mode,level)
            y=native(signal,fs,gain=1.)
            ref=oracle.drive(signal,fs,gain=1.,factor=32)
            rows.append(dict(group="boost_cascade",mode=oracle.BOOST_NAMES[mode],boost_db=level,
                             residual_dbr=oracle.residual(y,ref,fs)))
    # Latency measured from raw output, not merely trusting latencySamples().
    latency=[]
    for fs in oracle.P["supported_rates_hz"]:
        x=np.zeros(128);x[0]=1e-8;y=np.zeros_like(x)
        assert lib.ah_drive_render(fs,0.,.5,.5,10**(-.2),x,y,len(x),1)==32
        latency.append(int(np.argmax(abs(y))))
    assert latency==[32]*6
    assert worst_abs<2e-11 and worst_relative<2e-10 and volume_error<2e-11
    for row in rows:
        if "residual_dbr" in row: assert row["residual_dbr"] < -75.,row
    summary=dict(profile="JROCKETT-AH-DRIVE-BEHAVIORAL-V1",hardware_measured=False,
                 fidelity_cases=810,worst_fidelity=worst,max_oracle_absolute_error=worst_abs,
                 max_oracle_relative_error=worst_relative,volume_max_oracle_error=volume_error,
                 measured_latency_samples=latency,di_source=source,
                 volume_mapping="10^(12/20) * normalizedVolume^3",
                 default_volume=10**(-.2),records=len(rows))
    for file in (HERE.parent/"prototype.py",HERE.parent/"profile.json",HERE/"measure.py"):
        summary[str(file.relative_to(HERE.parent))+"_sha256"]=hashlib.sha256(file.read_bytes()).hexdigest()
    keys=list(dict.fromkeys(key for row in rows for key in row))
    with (args.out/"measurements.csv").open("w",newline="") as f:
        writer=csv.DictWriter(f,keys);writer.writeheader();writer.writerows(rows)
    (args.out/"results.json").write_text(json.dumps(summary,indent=2)+"\n")
    print(json.dumps(summary,indent=2))


if __name__=="__main__": main()
