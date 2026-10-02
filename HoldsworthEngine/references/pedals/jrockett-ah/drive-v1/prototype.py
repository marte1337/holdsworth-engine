#!/usr/bin/env python3
"""Offline design oracle only. No live processor, UI, NAM, or hardware fitting.

Writes five compact artifacts beside this script. --out can redirect them.
Python 3.9+, NumPy 2.0.2, SciPy 1.13.1, Matplotlib 3.9.4 used for retained run.
"""
import argparse
import csv
import hashlib
import json
import os
from functools import lru_cache
from pathlib import Path

os.environ.setdefault("MPLCONFIGDIR", "/private/tmp/ah-drive-matplotlib")
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import scipy
from scipy import signal
from scipy.io import wavfile

HERE = Path(__file__).resolve().parent
P = json.loads((HERE / "profile.json").read_text())
BOOST_CURVES = ((6, 0), (3, 3), (3, 0), (0, 3), (-3, 3), (-6, 6))
BOOST_NAMES = ("F/L", "F/H", "C/L", "C/H", "T/L", "T/H")
FNS = {"asinh": np.arcsinh, "tanh": np.tanh,
       "rational": lambda x: x / np.hypot(1.0, x)}


def db(x):
    return 20 * np.log10(np.maximum(np.abs(x), 1e-16))


def rms(x):
    return np.sqrt(np.mean(np.square(x)))


def tone_db(kind, setting):
    return P[kind+"_db_min"] + setting*(P[kind+"_db_max"]-P[kind+"_db_min"])


def shelf_coeff(amount, hz, fs, high=False):
    # Same analytic definition as Boost; independent offline transcription.
    if amount == 0:
        return np.array([1., 0.]), np.array([1., 0.])
    a = 10 ** (amount / 20)
    k = np.tan(np.pi * hz / fs)
    root = np.sqrt(a)
    if high:
        b = a * np.array([1 + k / root, -1 + k / root])
        den = np.array([1 + k * root, -1 + k * root])
    else:
        b = np.array([1 + k * root, -1 + k * root])
        den = np.array([1 + k / root, -1 + k / root])
    return b / den[0], den / den[0]


def shelf(x, amount, hz, fs, high=False, periodic=False):
    b, a = shelf_coeff(amount, hz, fs, high)
    if periodic:
        w = 2 * np.pi * np.fft.rfftfreq(len(x))
        h = signal.freqz(b, a, worN=w)[1]
        return np.fft.irfft(np.fft.rfft(x) * h, n=len(x))
    return signal.lfilter(b, a, x)


@lru_cache(None)
def fir(factor):
    return signal.firwin(P["fir_taps_per_factor"] * factor + 1, 1 / factor,
                         window=("kaiser", P["fir_kaiser_beta"]))


def boost(x, fs, mode, level, periodic=False):
    lo, hi = BOOST_CURVES[mode]
    return shelf(shelf(x, lo, 250., fs, periodic=periodic), hi, 2500., fs,
                 high=True, periodic=periodic) * 10 ** (level / 20)


def drive(x, fs, gain=.5, bass=.5, treble=.5, volume=0., topology="A",
          factor=None, transfer="asinh", periodic=False, ideal=False):
    factor = P["oversampling"] if factor is None else factor
    low = tone_db("bass", bass)
    high = tone_db("treble", treble)
    d = 10 ** ((P["gain_db_min"] + gain *
                (P["gain_db_max"] - P["gain_db_min"])) / 20)
    if topology == "A":
        x = shelf(x, low, P["bass_hz"], fs, periodic=periodic)
    pad = "wrap" if periodic else "constant"
    if ideal:
        assert periodic
        u = signal.resample(x, len(x) * factor)
    else:
        u = signal.resample_poly(x, factor, 1, window=fir(factor), padtype=pad)
    u = P["reference_volts"] * FNS[transfer](d * u / P["reference_volts"])
    u *= d ** (-P["gain_compensation_exponent"])
    if ideal:
        y = signal.resample(u, len(x))
    else:
        y = signal.resample_poly(u, 1, factor, window=fir(factor), padtype=pad)
    if topology == "B":
        y = shelf(y, low, P["bass_hz"], fs, periodic=periodic)
    y = shelf(y, high, P["treble_hz"], fs, high=True, periodic=periodic)
    return y * 10 ** (volume / 20)


def tone(fs, hz, amplitude, n=None):
    n = P["periodic_samples"] if n is None else n
    k = int(round(hz * n / fs)) | 1  # odd bin, long coherent period
    return amplitude * np.sin(2 * np.pi * k * np.arange(n) / n), k


def residual(y, ref, fs):
    f = np.fft.rfftfreq(len(y), 1 / fs)
    mask = (f >= P["fidelity_band_hz"][0]) & (f <= P["fidelity_band_hz"][1])
    err = np.fft.rfft(y - ref)[mask]
    den = np.fft.rfft(ref)[mask]
    return float(db(np.linalg.norm(err) / max(np.linalg.norm(den), 1e-30)))


def harmonics(y, k):
    a = 2 * np.abs(np.fft.rfft(y)) / len(y)
    hs = np.arange(1, min(32, (len(y) // 2) // k + 1)) * k
    return a[hs], float(100 * np.linalg.norm(a[hs[1:]]) / a[k])


def read_di():
    path = HERE.parents[4] / "REAPER/Guitar DI.wav"
    fs, raw = wavfile.read(path)
    x = raw.astype(np.float64) / (2 ** (np.iinfo(raw.dtype).bits - 1))
    if x.ndim > 1:
        x = x.mean(axis=1)
    # Deterministically use the highest-energy two-second window, 0.25 s grid.
    length = min(len(x), 2 * fs)
    starts = range(0, max(1, len(x) - length + 1), fs // 4)
    start = max(starts, key=lambda i: float(np.sum(x[i:i + length] ** 2)))
    x = x[start:start + length].copy()
    ramp = min(int(.01 * fs), len(x) // 4)
    x[:ramp] *= np.linspace(0, 1, ramp)
    x[-ramp:] *= np.linspace(1, 0, ramp)
    x *= .2 / np.max(np.abs(x))
    return fs, x, {"path": "REAPER/Guitar DI.wav", "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                   "start_seconds": start / fs, "length_seconds": len(x) / fs,
                   "rescaled_peak_software_volts": .2, "original_voltage_unknown": True}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", type=Path, default=HERE)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    records = []
    result = {"profile": P, "numpy": np.__version__, "scipy": scipy.__version__,
              "hardware_measurements": False}
    # Candidate transfer behavior and spectra. All use the same gain staging.
    for fn in FNS:
        for gain in (0., .5, 1.):
            for amp in (.02, .05, .2, .5):
                x, k = tone(48000, 440., amp)
                y = drive(x, 48000, gain=gain, transfer=fn, periodic=True)
                h, thd = harmonics(y, k)
                records.append(dict(group="transfer", name=fn, gain=gain, input_peak=amp,
                                    rms=float(rms(y)), fundamental=float(h[0]), thd_percent=thd))
                if fn == "asinh":
                    for j, value in enumerate(h):
                        records.append(dict(group="harmonic", name=fn, gain=gain,
                                            input_peak=amp, harmonic=j+1, db_relative=float(db(value/h[0]))))
    # Topology A/B: bass changes low-note harmonic creation only in A.
    for layout in ("A", "B"):
        for bass in (0., .5, 1.):
            x, k = tone(48000, 110., .2)
            y = drive(x, 48000, gain=.75, bass=bass, topology=layout, periodic=True)
            h, thd = harmonics(y, k)
            records.append(dict(group="topology", name=layout, bass=bass,
                                thd_percent=thd, fundamental=float(h[0]), h3_dbr=float(db(h[2]/h[0]))))
    # Exact small-signal shelf responses, plus nonlinear fundamental response.
    for kind in ("bass", "treble"):
        for setting in (0., .5, 1.):
            for hz in (40., 80., 110., 250., 1000., 2500., 5000., 10000.):
                x, k = tone(48000, hz, .2)
                options = {kind: setting}
                y = drive(x, 48000, periodic=True, **options)
                h, thd = harmonics(y, k)
                b, a = shelf_coeff(tone_db(kind,setting), P[kind + "_hz"], 48000, kind == "treble")
                eq_db = float(db(signal.freqz(b, a, worN=[2*np.pi*hz/48000])[1][0]))
                records.append(dict(group="tone", name=kind, setting=setting, hz=hz,
                                    shelf_db=eq_db, fundamental_db=float(db(h[0]/.2)), thd_percent=thd))
    # Predeclared product grid; report full residual, not a pure alias estimate.
    # Includes full-amplitude 4 kHz edge cases and quieter 6/8 kHz overtones.
    product = []
    for fs in P["supported_rates_hz"]:
        for gain in (0., .5, 1.):
            for hz, amp in ((110,.02),(440,.05),(440,.2),(110,.5),(1000,.5),
                            (2000,.5),(4000,.5),(6000,.05),(8000,.05)):
                for bass, treble in ((.5,.5),(0.,0.),(0.,1.),(1.,0.),(1.,1.)):
                    x, k = tone(fs, hz, amp)
                    options = dict(gain=gain, bass=bass, treble=treble, periodic=True)
                    ref = drive(x, fs, factor=P["reference_factor"], **options)
                    for factor in (2,4):
                        y = drive(x, fs, factor=factor, **options)
                        row = dict(group="fidelity", name="product", fs=fs, gain=gain,
                                   hz=k*fs/len(x), input_peak=amp, bass=bass, treble=treble,
                                   factor=factor, residual_dbr=residual(y,ref,fs))
                        product.append(row)
                        records.append(row)
        print("Completed product rate", fs, flush=True)
    result["product"] = {}
    for factor in (2,4):
        rows = [r for r in product if r["factor"] == factor]
        worst = max(rows, key=lambda r:r["residual_dbr"])
        result["product"][str(factor)] = dict(cases=len(rows), worst=worst,
                    failures=sum(r["residual_dbr"] > P["fidelity_residual_limit_dbr"] for r in rows))
    # Independent ideal-bandlimited reference and convergence at worst 4x case.
    w = result["product"]["4"]["worst"]
    x, _ = tone(w["fs"], w["hz"], w["input_peak"])
    opts = dict(gain=w["gain"], bass=w["bass"], treble=w["treble"], periodic=True)
    r32 = drive(x,w["fs"],factor=32,**opts)
    r64 = drive(x,w["fs"],factor=64,**opts)
    ideal64 = drive(x,w["fs"],factor=64,ideal=True,**opts)
    result["reference_checks"] = {"32_vs_64_dbr":residual(r32,r64,w["fs"]),
                                 "64_fir_vs_ideal_dbr":residual(r64,ideal64,w["fs"]),
                                 "4_fir_vs_ideal_dbr":residual(drive(x,w["fs"],factor=4,**opts),ideal64,w["fs"])}
    # Retain the bounded candidate antialias comparison, not an expanding search.
    for fn in FNS:
        for hz in (2000.,4000.):
            x,_=tone(44100,hz,.5)
            ref=drive(x,44100,gain=1.,transfer=fn,factor=32,periodic=True)
            for factor in (2,4):
                y=drive(x,44100,gain=1.,transfer=fn,factor=factor,periodic=True)
                records.append(dict(group="candidate_alias",name=fn,hz=hz,factor=factor,
                                    residual_dbr=residual(y,ref,44100)))
    # Multitone intermodulation and separately-labelled overload/near-Nyquist.
    for fs in P["supported_rates_hz"]:
        for case in ("guitar_multitone", "hot_2V", "torture_10V_nyquist"):
            if case == "torture_10V_nyquist":
                x, _ = tone(fs, .45*fs, 10.)
            else:
                x = sum(tone(fs,hz,a)[0] for hz,a in ((110,1.),(220,.5),(330,.25),
                            (880,.12),(1760,.06),(3520,.03),(7040,.015)))
                x *= (.5 if case == "guitar_multitone" else 2.)/max(abs(x))
            ref = drive(x, fs, gain=1., bass=1., treble=1., factor=32, periodic=True)
            for factor in (2,4):
                y=drive(x,fs,gain=1.,bass=1.,treble=1.,factor=factor,periodic=True)
                records.append(dict(group="extended",name=case,fs=fs,factor=factor,
                                    residual_dbr=residual(y,ref,fs),
                                    residual_fullband_dbr=float(db(rms(y-ref)/rms(ref))),
                                    peak=float(max(abs(y))),finite=bool(np.isfinite(y).all())))
    # DI cleanup and cascade. Pad and crop 100 ms for nonperiodic FIR boundaries.
    fs, di, result["di"] = read_di()
    dpad = np.pad(di, (fs//10,fs//10))
    di_cases=[]
    for gain in (0.,.5,1.):
        for peak in (.05,.2,.5):
            di_cases.append((gain,peak,.5,.5))
    di_cases.extend((1.,.5,b,t) for b,t in ((0.,0.),(0.,1.),(1.,0.),(1.,1.)))
    di_rows=[]
    for gain,peak,bass,treble in di_cases:
        x=dpad*(peak/.2)
        opts=dict(gain=gain,bass=bass,treble=treble)
        ref=drive(x,fs,factor=32,**opts)
        y=drive(x,fs,factor=4,**opts)
        row=dict(group="di_fidelity",name="guitar_DI",gain=gain,input_peak=peak,
                 bass=bass,treble=treble,residual_dbr=residual(y,ref,fs))
        records.append(row);di_rows.append(row)
    result["di_worst"]=max(di_rows,key=lambda r:r["residual_dbr"])
    cascade = []
    for mode in range(6):
        for level in (0.,6.,12.,20.):
            x=boost(dpad*.25,fs,mode,level)
            ref=drive(x,fs,gain=1.,factor=32)
            y=drive(x,fs,gain=1.,factor=4)
            row=dict(group="cascade",name=BOOST_NAMES[mode],boost_db=level,
                     input_peak=float(max(abs(x))),residual_dbr=residual(y,ref,fs),rms=float(rms(y)))
            records.append(row); cascade.append(row)
    result["cascade_worst"] = max(cascade,key=lambda r:r["residual_dbr"])
    # Confirm settled offline Boost against the frozen production M1 measurements.
    boost_error=0.
    with (HERE.parent/"m1/responses.csv").open() as f:
        for row in csv.DictReader(f):
            rate=float(row["sample_rate_hz"]);hz=float(row["frequency_hz"])
            lo,hi=BOOST_CURVES[BOOST_NAMES.index(row["mode"])]
            h=1.+0j
            for amount,center,high in ((lo,250.,False),(hi,2500.,True)):
                b,a=shelf_coeff(amount,center,rate,high)
                h*=signal.freqz(b,a,worN=[2*np.pi*hz/rate])[1][0]
            boost_error=max(boost_error,abs(float(db(h))-float(row["measured_db"])))
    result["boost_reference_max_db_error"]=boost_error
    # Post-volume must be a scalar; silence/odd symmetry and low-level slope.
    base=drive(dpad,fs)
    result["checks"] = {
        "volume_scalar_max_error":float(max(abs(drive(dpad,fs,volume=-12)-base*10**(-12/20)))),
        "odd_symmetry_max_error":float(max(abs(drive(-dpad,fs)+base))),
        "silence_peak":float(max(abs(drive(np.zeros(4096),fs)))),
        "unity_low_gain_slope_error":float(abs(np.arcsinh(1e-8)/1e-8-1)),
    }
    # Causal FIR realization of the centered offline reference: 32 base samples.
    x=np.pad(di[:4096],(128,128))
    d=10**(P["gain_db_max"]*.5/20)
    u=signal.upfirdn(fir(4)*4,x,up=4)
    u=np.arcsinh(d*u)*d**(-P["gain_compensation_exponent"])
    causal=signal.upfirdn(fir(4),u,down=4)
    centered=drive(x,fs)
    result["checks"]["causal_shift_32_max_error"]=float(max(abs(causal[32:32+len(x)]-centered)))
    # Filter specifications: one stage, before nonlinear complications.
    ff, hh=signal.freqz(fir(4),worN=131072,fs=4*44100)
    result["fir"]={"taps_each_direction":len(fir(4)),"latency_base_samples":32,
                   "passband_20_to_10000_db_max_abs":float(max(abs(db(hh[(ff>=20)&(ff<=10000)])))),
                   "stopband_from_0_625_base_fs_db_max":float(max(db(hh[ff>=.625*44100])))}
    assert result["product"]["4"]["failures"] == 0
    assert result["cascade_worst"]["residual_dbr"] < P["fidelity_residual_limit_dbr"]
    assert result["di_worst"]["residual_dbr"] < P["fidelity_residual_limit_dbr"]
    assert result["boost_reference_max_db_error"] < 1e-8
    assert result["reference_checks"]["32_vs_64_dbr"] < -100
    assert result["reference_checks"]["4_fir_vs_ideal_dbr"] < P["fidelity_residual_limit_dbr"]
    assert all(v < 1e-12 for v in result["checks"].values())
    # Two audio files only. Fixed gain montage preserves dynamics and loudness;
    # stereo topology montage separately RMS-matches channels for timbre comparison.
    gap=np.zeros(fs//4); manifest=[]; blocks=[]; position=0
    audio_cases=[("dry",None,dpad)]
    for gain in (0.,.5,1.):
        for scale in (1.,.25):
            audio_cases.append(("gain_%g_input_%gV"%(gain,.2*scale),gain,dpad*scale))
    for level in (0.,6.,12.,20.):
        audio_cases.append(("F_H_boost_%gdB_gain_mid"%level,.5,boost(dpad*.25,fs,1,level)))
    for name,gain,x in audio_cases:
        y=x if gain is None else drive(x,fs,gain=gain)
        y=y[fs//10:-fs//10]
        blocks.extend((y,gap)); manifest.append(dict(file="gain-cleanup-boost.wav",name=name,start_seconds=position/fs,duration_seconds=len(y)/fs))
        position+=len(y)+len(gap)
    gain_audio=np.concatenate(blocks); fixed=.85/max(1.,max(abs(gain_audio)))
    wavfile.write(args.out/"gain-cleanup-boost.wav",fs,np.rint(gain_audio*fixed*32767).astype(np.int16))
    result["gain_audio_common_scale"] = fixed
    blocks=[];position=0
    for kind in ("bass","treble"):
        for setting in (0.,.5,1.):
            options={kind:setting}
            a=drive(dpad,fs,gain=.75,topology="A",**options)[fs//10:-fs//10]
            b=drive(dpad,fs,gain=.75,topology="B",**options)[fs//10:-fs//10]
            scales=(.12/rms(a),.12/rms(b))
            stereo=np.column_stack((a*scales[0],b*scales[1]))
            blocks.extend((stereo,np.zeros((len(gap),2))))
            manifest.append(dict(file="tone-topology-AB.wav",name=kind+"_%g"%setting,
                                 start_seconds=position/fs,duration_seconds=len(a)/fs,
                                 left="A",right="B",left_scale=float(scales[0]),right_scale=float(scales[1])))
            position+=len(a)+len(gap)
    audio=np.concatenate(blocks)
    assert np.max(abs(audio))<1
    wavfile.write(args.out/"tone-topology-AB.wav",fs,np.rint(audio*32767).astype(np.int16))
    result["audio_cues"] = manifest
    # One compact figure: transfer, harmonic progression, tone, convergence.
    fig,axes=plt.subplots(2,2,figsize=(11,7),layout="constrained")
    u=np.linspace(-8,8,1000)
    for name,fn in FNS.items(): axes[0,0].plot(u,fn(u),label=name)
    axes[0,0].set(title="Candidate transfers (unit slope at zero)",xlabel="Nonlinear input u",ylabel="f(u)")
    for gain in (0.,.5,1.):
        rows=[r for r in records if r["group"]=="harmonic" and r["gain"]==gain and r["input_peak"]==.2 and r["harmonic"]%2]
        axes[0,1].plot([r["harmonic"] for r in rows],[r["db_relative"] for r in rows],'.-',label="Gain %g"%gain)
    axes[0,1].set(title="asinh: ~440 Hz, 0.2 V peak",xlabel="Harmonic",ylabel="dB relative to fundamental",ylim=(-120,5))
    for kind in ("bass","treble"):
        for setting in (0.,1.):
            f=np.geomspace(20,20000,300)
            b,a=shelf_coeff(tone_db(kind,setting),P[kind+"_hz"],48000,kind=="treble")
            axes[1,0].semilogx(f,db(signal.freqz(b,a,worN=2*np.pi*f/48000)[1]),label=kind+" %g"%setting)
    axes[1,0].set(title="Software tone shelves, 48 kHz",xlabel="Hz",ylabel="dB")
    for factor in (2,4):
        rates=P["supported_rates_hz"]
        values=[max(r["residual_dbr"] for r in product if r["fs"]==fs and r["factor"]==factor) for fs in rates]
        axes[1,1].plot(np.array(rates)/1000,values,'.-',label="%dx"%factor)
    axes[1,1].axhline(P["fidelity_residual_limit_dbr"],color="gray",linestyle="--",label="Proposed gate")
    axes[1,1].set(title="Worst product-grid residual vs 32x",xlabel="Base rate (kHz)",ylabel="20 Hz–10 kHz residual (dBr)")
    for ax in axes.flat: ax.grid(alpha=.2);ax.legend(fontsize=8)
    fig.suptitle(P["id"]+" · offline software measurements",fontsize=12)
    fig.savefig(args.out/"overview.png",dpi=130);plt.close(fig)
    keys=list(dict.fromkeys(k for row in records for k in row))
    with (args.out/"measurements.csv").open("w",newline="") as f:
        writer=csv.DictWriter(f,keys);writer.writeheader();writer.writerows(records)
    result["record_count"] = len(records)
    for filename in ("prototype.py","profile.json"):
        result[filename+"_sha256"] = hashlib.sha256((HERE/filename).read_bytes()).hexdigest()
    (args.out/"results.json").write_text(json.dumps(result,indent=2)+"\n")
    print(json.dumps({k:result[k] for k in ("product","reference_checks","cascade_worst","checks")},indent=2))


if __name__ == "__main__":
    main()
