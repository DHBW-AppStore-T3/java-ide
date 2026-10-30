import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.SocketTimeoutException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/**
 * Checks the UDP broadcast of the image (image/peerbroadcast): a program sends to a broadcast or multicast address and
 * every address of the name "peers" receives the datagram exactly once; a unicast send reaches only its target.
 *
 * The caller makes "peers" resolve to the receiver addresses (docker run --add-host peers:127.0.0.2 --add-host
 * peers:127.0.0.3) and runs this once with and once without -Djava.net.preferIPv4Stack=true (IPv4 and dual-stack
 * socket). Arguments: the receiver addresses. Exit code 0 when every check holds.
 */
public class BroadcastCheck {
    static final int PORT = 47321;
    static int failures = 0;

    public static void main(String[] args) throws Exception {
        List<DatagramSocket> receivers = new ArrayList<>();
        for (String a : args) {
            DatagramSocket s = new DatagramSocket(null);
            s.setReuseAddress(true);
            s.bind(new InetSocketAddress(InetAddress.getByName(a), PORT));
            s.setSoTimeout(1500);
            receivers.add(s);
        }
        try (DatagramSocket sender = new DatagramSocket()) {
            sender.setBroadcast(true);

            // Broadcast and multicast: every receiver gets it once, none gets it twice.
            List<String> groups = new ArrayList<>(List.of("255.255.255.255", "224.0.0.1"));
            // Optionally the directed broadcast address of a local subnet (-Ddirected=172.17.255.255).
            if (System.getProperty("directed") != null) groups.add(System.getProperty("directed"));
            for (String group : groups) {
                send(sender, group, "to-" + group);
                for (int i = 0; i < receivers.size(); i++) {
                    String got = receive(receivers.get(i));
                    check(group + " reaches " + args[i], "to-" + group, got);
                    check(group + " arrives once at " + args[i], null, receive(receivers.get(i)));
                }
            }

            // Unicast stays unicast: only the addressed receiver gets it.
            send(sender, args[0], "only-first");
            check("unicast reaches " + args[0], "only-first", receive(receivers.get(0)));
            for (int i = 1; i < receivers.size(); i++) {
                check("unicast does not reach " + args[i], null, receive(receivers.get(i)));
            }
        }
        for (DatagramSocket s : receivers) s.close();
        if (failures > 0) {
            System.out.println("FAILED: " + failures + " check(s)");
            System.exit(1);
        }
        System.out.println("broadcast-ok");
    }

    static void send(DatagramSocket socket, String to, String text) throws Exception {
        byte[] data = text.getBytes(StandardCharsets.UTF_8);
        socket.send(new DatagramPacket(data, data.length, InetAddress.getByName(to), PORT));
    }

    /** The next datagram as text, or null if none arrives in time. */
    static String receive(DatagramSocket socket) throws Exception {
        byte[] buf = new byte[512];
        DatagramPacket p = new DatagramPacket(buf, buf.length);
        try {
            socket.receive(p);
        } catch (SocketTimeoutException e) {
            return null;
        }
        return new String(p.getData(), 0, p.getLength(), StandardCharsets.UTF_8);
    }

    static void check(String what, String expected, String actual) {
        boolean ok = expected == null ? actual == null : expected.equals(actual);
        System.out.println((ok ? "ok   " : "FAIL ") + what + (ok ? "" : " (expected " + expected + ", got " + actual + ")"));
        if (!ok) failures++;
    }
}
