using System;
using System.Collections.Generic;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Text;
using LibreHardwareMonitor.Hardware;

namespace HardwareWrapper
{
    public static class LhmNativeApi
    {
        private static Computer? _computer;

        [UnmanagedCallersOnly(EntryPoint = "InitHardwareMonitor")]
        public static bool InitHardwareMonitor()
        {
            try
            {
                _computer = new Computer
                {
                    IsCpuEnabled = true,
                    IsGpuEnabled = true,
                    IsMotherboardEnabled = true
                };
                _computer.Open();
                return true;
            }
            catch
            {
                return false;
            }
        }

        [UnmanagedCallersOnly(EntryPoint = "GetTemperaturesJson")]
        public static IntPtr GetTemperaturesJson()
        {
            string jsonResult = "[]";

            try
            {
                if (_computer != null)
                {
                    var items = new List<string>();

                    foreach (IHardware hardware in _computer.Hardware)
                    {
                        try
                        {
                            hardware.Update();
                            
                            var allHardware = new List<IHardware> { hardware };
                            if (hardware.SubHardware != null)
                            {
                                allHardware.AddRange(hardware.SubHardware);
                            }

                            foreach (var hw in allHardware)
                            {
                                try
                                {
                                    hw.Update();
                                    foreach (ISensor sensor in hw.Sensors)
                                    {
                                        if (sensor.SensorType == SensorType.Temperature && sensor.Value.HasValue)
                                        {
                                            string cleanName = $"{hw.Name} - {sensor.Name}".Replace("\"", "\\\"");
                                            string valStr = sensor.Value.Value.ToString("F1", CultureInfo.InvariantCulture);
                                            
                                            items.Add($"{{\"name\":\"{cleanName}\",\"value\":{valStr}}}");
                                        }
                                    }
                                }
                                catch { /* Игнорируем ошибки сбоя отдельных датчиков */ }
                            }
                        }
                        catch { /* Игнорируем ошибки конкретного железа */ }
                    }

                    jsonResult = "[" + string.Join(",", items) + "]";
                }
            }
            catch
            {
                jsonResult = "[]";
            }

            // Выделяем память под C-строку
            byte[] bytes = Encoding.UTF8.GetBytes(jsonResult + "\0");
            IntPtr mem = Marshal.AllocHGlobal(bytes.Length);
            Marshal.Copy(bytes, 0, mem, bytes.Length);

            return mem;
        }

        [UnmanagedCallersOnly(EntryPoint = "FreeJsonBuffer")]
        public static void FreeJsonBuffer(IntPtr ptr)
        {
            if (ptr != IntPtr.Zero)
            {
                Marshal.FreeHGlobal(ptr);
            }
        }

        [UnmanagedCallersOnly(EntryPoint = "CloseHardwareMonitor")]
        public static void CloseHardwareMonitor()
        {
            try
            {
                _computer?.Close();
            }
            catch { }
        }
    }
}