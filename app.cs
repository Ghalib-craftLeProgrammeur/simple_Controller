using System;
using System.Drawing;
using System.IO;
using System.IO.Pipes;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Windows.Forms;

public class TrayApp : ApplicationContext
{
    private NotifyIcon trayIcon;
    private ContextMenuStrip trayMenu;
    private bool isRunning = false;

    private TcpListener? tcpListener;
    private Thread? listenerThread;
    private bool listenerRunning = false;

    private NamedPipeServerStream? pipeStream;
    private StreamWriter? pipeWriter;

    public TrayApp()
    {
        trayMenu = new ContextMenuStrip();
        trayMenu.Items.Add("Start GhalibVR", null, OnStartGhalibVR);
        trayMenu.Items.Add("Stop GhalibVR", null, OnStopGhalibVR);
        trayMenu.Items.Add("Exit", null, OnExit);

        trayIcon = new NotifyIcon
        {
            Text = "GhalibVR App",
            Icon = new Icon("ghalibvr.ico"),
            ContextMenuStrip = trayMenu,
            Visible = true
        };

        trayIcon.DoubleClick += (sender, e) => { OnStartGhalibVR(sender, e); };
    }

    private void OnStartGhalibVR(object? sender, EventArgs e)
    {
        if (isRunning)
        {
            MessageBox.Show("GhalibVR is already running.");
            return;
        }

        try
        {
            // Create the Named Pipe server asynchronously
            pipeStream = new NamedPipeServerStream("GhalibVRPipe", PipeDirection.Out, 1, PipeTransmissionMode.Byte, PipeOptions.Asynchronous);
          // Begin waiting for client connection
            pipeStream.BeginWaitForConnection(ar =>
            {
                try
                {
                    pipeStream.EndWaitForConnection(ar);
                    pipeWriter = new StreamWriter(pipeStream) { AutoFlush = true };
                    MessageBox.Show("Named Pipe connection established.");
                }
                catch (Exception ex)
                {
                    MessageBox.Show("Pipe connection error: " + ex.Message);
                }
            }, null);

            StartTcpListener();
            isRunning = true;
            MessageBox.Show("GhalibVR started!");
        }
        catch (Exception ex)
        {
            MessageBox.Show("Error starting GhalibVR: " + ex.Message);
        }
    }

    private void StartTcpListener()
    {
        tcpListener = new TcpListener(127.0.0.1, 4120);
        tcpListener.Start();

        listenerRunning = true;
        listenerThread = new Thread(ListenForClients)
        {
            IsBackground = true
        };
        listenerThread.Start();
    }

    private void ListenForClients()
    {
        while (listenerRunning)
        {
            try
            {
                if (!tcpListener!.Pending())
                {
                    Thread.Sleep(100);  // Avoid high CPU usage
                    continue;
                }

                TcpClient tcpClient = tcpListener.AcceptTcpClient();
                Thread clientThread = new Thread(() => HandleClientComm(tcpClient));
                clientThread.Start();
            }
            catch (SocketException) { break; }
            catch (Exception ex)
            {
                MessageBox.Show("Listener error: " + ex.Message);
            }
        }
    }

    private void HandleClientComm(TcpClient tcpClient)
    {
        using (tcpClient)
        using (NetworkStream stream = tcpClient.GetStream())
        {
            byte[] buffer = new byte[1024];
            int bytesRead;

            while ((bytesRead = stream.Read(buffer, 0, buffer.Length)) != 0)
            {
                string dataReceived = Encoding.ASCII.GetString(buffer, 0, bytesRead);
                SendDataToPipe(dataReceived);
            }
        }
    }

    private void SendDataToPipe(string data)
    {
        try
        {
            if (pipeWriter != null)
            {
                pipeWriter.WriteLine(data); // Adds newline after writing
            }
        }
        catch (Exception ex)
        {
            MessageBox.Show("Pipe write error: " + ex.Message);
        }
    }

    private void OnStopGhalibVR(object? sender, EventArgs e)
    {
        if (!isRunning)
        {
            MessageBox.Show("GhalibVR is not running.");
            return;
        }

        try
        {
            isRunning = false;
            listenerRunning = false;
            tcpListener?.Stop();

            pipeWriter?.Close();
            pipeStream?.Close();

            MessageBox.Show("GhalibVR stopped!");
        }
        catch (Exception ex)
        {
            MessageBox.Show("Error stopping GhalibVR: " + ex.Message);
        }
    }

    private void OnExit(object? sender, EventArgs e)
    {
        listenerRunning = false;
        tcpListener?.Stop();
        pipeWriter?.Close();
        pipeStream?.Close();

        trayIcon.Visible = false;
        Application.Exit();
    }
}
