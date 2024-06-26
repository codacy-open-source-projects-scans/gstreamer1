// This file was generated by the Gtk# code generator.
// Any changes made will be lost if regenerated.

namespace Gst.RtspServer {

	using System;
	using System.Collections;
	using System.Collections.Generic;
	using System.Runtime.InteropServices;

#region Autogenerated code
	[StructLayout(LayoutKind.Sequential)]
	public partial struct RTSPAddress : IEquatable<RTSPAddress> {

		private IntPtr _pool;
		public Gst.RtspServer.RTSPAddressPool Pool {
			get {
				return GLib.Object.GetObject(_pool) as Gst.RtspServer.RTSPAddressPool;
			}
			set {
				_pool = value == null ? IntPtr.Zero : value.Handle;
			}
		}
		public string Address;
		public ushort Port;
		public int NPorts;
		public byte Ttl;
		private IntPtr _priv;

		public static Gst.RtspServer.RTSPAddress Zero = new Gst.RtspServer.RTSPAddress ();

		public static Gst.RtspServer.RTSPAddress New(IntPtr raw) {
			if (raw == IntPtr.Zero)
				return Gst.RtspServer.RTSPAddress.Zero;
			return (Gst.RtspServer.RTSPAddress) Marshal.PtrToStructure (raw, typeof (Gst.RtspServer.RTSPAddress));
		}

		[DllImport("gstrtspserver-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr gst_rtsp_address_get_type();

		public static GLib.GType GType { 
			get {
				IntPtr raw_ret = gst_rtsp_address_get_type();
				GLib.GType ret = new GLib.GType(raw_ret);
				return ret;
			}
		}

		public bool Equals (RTSPAddress other)
		{
			return true && Pool.Equals (other.Pool) && Address.Equals (other.Address) && Port.Equals (other.Port) && NPorts.Equals (other.NPorts) && Ttl.Equals (other.Ttl) && _priv.Equals (other._priv);
		}

		public override bool Equals (object other)
		{
			return other is RTSPAddress && Equals ((RTSPAddress) other);
		}

		public override int GetHashCode ()
		{
			return this.GetType ().FullName.GetHashCode () ^ Pool.GetHashCode () ^ Address.GetHashCode () ^ Port.GetHashCode () ^ NPorts.GetHashCode () ^ Ttl.GetHashCode () ^ _priv.GetHashCode ();
		}

		public static explicit operator GLib.Value (Gst.RtspServer.RTSPAddress boxed)
		{
			GLib.Value val = GLib.Value.Empty;
			val.Init (Gst.RtspServer.RTSPAddress.GType);
			val.Val = boxed;
			return val;
		}

		public static explicit operator Gst.RtspServer.RTSPAddress (GLib.Value val)
		{
			return (Gst.RtspServer.RTSPAddress) val.Val;
		}
#endregion
	}
}
