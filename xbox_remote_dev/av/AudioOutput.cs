using System;
using System.Runtime.InteropServices;

namespace XboxRemoteDev.AV {
    // Four WinMM buffers maximum, with a total queue budget of 100 ms.
    internal sealed class AudioOutput : IDisposable {
        [StructLayout(LayoutKind.Sequential, Pack=2)]
        private struct Format {
            internal ushort tag,channels;
            internal uint rate,bytesPerSecond;
            internal ushort align,bits,extra;
        }
        [StructLayout(LayoutKind.Sequential)]
        private struct Header {
            internal IntPtr data;
            internal uint length,recorded;
            internal UIntPtr user;
            internal uint flags,loops;
            internal IntPtr next;
            internal UIntPtr reserved;
        }
        [DllImport("winmm.dll")] private static extern uint waveOutOpen(out IntPtr h,uint id,ref Format f,IntPtr cb,IntPtr instance,uint flags);
        [DllImport("winmm.dll")] private static extern uint waveOutPrepareHeader(IntPtr h,IntPtr hdr,uint size);
        [DllImport("winmm.dll")] private static extern uint waveOutUnprepareHeader(IntPtr h,IntPtr hdr,uint size);
        [DllImport("winmm.dll")] private static extern uint waveOutWrite(IntPtr h,IntPtr hdr,uint size);
        [DllImport("winmm.dll")] private static extern uint waveOutReset(IntPtr h);
        [DllImport("winmm.dll")] private static extern uint waveOutClose(IntPtr h);
        private readonly IntPtr[] headers=new IntPtr[4], data=new IntPtr[4];
        private readonly bool[] prepared=new bool[4];
        private readonly int[] sizes=new int[4];
        private IntPtr device;
        private int rate,bits;
        private static readonly uint HeaderSize=(uint)Marshal.SizeOf(typeof(Header));
        internal long DroppedPackets {get;private set;}
        private static void Check(uint result,string op) { if(result!=0)throw new InvalidOperationException(op+" failed, WinMM="+result); }
        private void Open(int hz,int depth) {
            if(device!=IntPtr.Zero && rate==hz && bits==depth)return;
            Dispose();
            Format f=new Format {tag=1,channels=2,rate=(uint)hz,bits=(ushort)depth,
                align=(ushort)(depth/4),bytesPerSecond=(uint)(hz*depth/4)};
            Check(waveOutOpen(out device,UInt32.MaxValue,ref f,IntPtr.Zero,IntPtr.Zero,0),"waveOutOpen");
            rate=hz;bits=depth;
            for(int i=0;i<4;i++) {
                data[i]=Marshal.AllocHGlobal(19200);
                headers[i]=Marshal.AllocHGlobal((int)HeaderSize);
            }
        }
        internal int Submit(Packet p) {
            Open(p.Quality,p.Height);
            int free=-1,queued=0;
            for(int i=0;i<4;i++) {
                if(prepared[i]) {
                    Header h=(Header)Marshal.PtrToStructure(headers[i],typeof(Header));
                    if((h.flags&1)!=0) { Check(waveOutUnprepareHeader(device,headers[i],HeaderSize),"unprepare"); prepared[i]=false; }
                    else queued+=sizes[i];
                }
                if(!prepared[i] && free<0)free=i;
            }
            int budget=rate*(bits/4)/10;
            int count=Math.Min(p.Payload.Length,budget);
            if(free<0 || queued+count>budget) { DroppedPackets++; return 0; }
            if(count!=p.Payload.Length)DroppedPackets++;
            Marshal.Copy(p.Payload,p.Payload.Length-count,data[free],count);
            Header next=new Header {data=data[free],length=(uint)count};
            Marshal.StructureToPtr(next,headers[free],false);
            Check(waveOutPrepareHeader(device,headers[free],HeaderSize),"prepare"); prepared[free]=true;
            sizes[free]=count;
            Check(waveOutWrite(device,headers[free],HeaderSize),"write");
            return count;
        }
        public void Dispose() {
            if(device==IntPtr.Zero)return;
            // Never free memory while the device can still own it.
            Check(waveOutReset(device),"reset");
            for(int i=0;i<4;i++) {
                if(prepared[i]) { Check(waveOutUnprepareHeader(device,headers[i],HeaderSize),"unprepare"); prepared[i]=false; }
                if(headers[i]!=IntPtr.Zero)Marshal.FreeHGlobal(headers[i]);
                if(data[i]!=IntPtr.Zero)Marshal.FreeHGlobal(data[i]);
                headers[i]=data[i]=IntPtr.Zero;
            }
            Check(waveOutClose(device),"close"); device=IntPtr.Zero;
        }
    }
}
