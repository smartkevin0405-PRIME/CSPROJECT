// 생성형 AI(Claude)의 도움을 받아 작성함.
package kr.doorboard.travellearn.core;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.Locale;
import java.util.Map;

import javax.net.SocketFactory;

/**
 * 통신규격용 최소 HTTP/1.1 구현(요청 1건 = 연결 1개, Connection: close).
 * 소켓을 직접 쓰는 이유: 본문 바이트를 그대로 MAC에 쓰고, Android에서 Wi-Fi 망에 묶인 SocketFactory를 쓰기 위해서다.
 */
public final class HttpMini {
    private HttpMini() {}

    public static final class Request {
        public String method;
        public String path;
        public final Map<String, String> headers = new HashMap<String, String>();
        public String body = "";
    }

    public static final class Response {
        public int status;
        public final Map<String, String> headers = new HashMap<String, String>();
        public String body = "";

        public Response() {}

        public Response(int status, String body) {
            this.status = status;
            this.body = body;
        }
    }

    public static final class HttpError extends IOException {
        public final int status;
        public final String err;

        public HttpError(int status, String err) {
            super("HTTP " + status + " " + err);
            this.status = status;
            this.err = err;
        }
    }

    private static final int MAX_LINE = 512;
    private static final int MAX_HEADERS = 24;
    private static final int MAX_HEADER_BYTES = 4096;   // 요청 줄 + 헤더 전체(CR 포함) 상한 (B16)

    /** 마감 시각과 헤더 바이트 상한을 지키는 입력 스트림 (B16) */
    private static final class Guarded extends InputStream {
        private final InputStream in;
        private final long deadlineNs;
        private int headerLeft = MAX_HEADER_BYTES;
        private boolean inBody;

        Guarded(InputStream in, long deadlineNs) {
            this.in = in;
            this.deadlineNs = deadlineNs;
        }

        void headerDone() {
            inBody = true;
        }

        private void check() throws IOException {
            if (deadlineNs != 0 && System.nanoTime() - deadlineNs > 0) throw new IOException("요청 시간 초과");
        }

        @Override
        public int read() throws IOException {
            check();
            int c = in.read();
            if (c >= 0 && !inBody && --headerLeft < 0) throw new HttpError(400, "json");
            return c;
        }

        @Override
        public int read(byte[] b, int off, int len) throws IOException {
            check();
            int n = in.read(b, off, len);
            check();
            return n;
        }
    }

    private static String readLine(InputStream in) throws IOException {
        ByteArrayOutputStream buf = new ByteArrayOutputStream(96);
        while (true) {
            int c = in.read();
            if (c < 0) {
                if (buf.size() == 0) return null;
                break;
            }
            if (c == '\n') break;
            if (c != '\r') buf.write(c);
            if (buf.size() > MAX_LINE) throw new HttpError(400, "json");
        }
        return new String(buf.toByteArray(), StandardCharsets.UTF_8);
    }

    private static void readHeaders(InputStream in, Map<String, String> headers) throws IOException {
        for (int i = 0; i < MAX_HEADERS + 1; i++) {
            String line = readLine(in);
            if (line == null || line.isEmpty()) return;
            int colon = line.indexOf(':');
            if (colon <= 0) continue;
            headers.put(line.substring(0, colon).trim().toLowerCase(Locale.ROOT),
                    line.substring(colon + 1).trim());
        }
        throw new HttpError(400, "json");
    }

    private static String readBody(InputStream in, Map<String, String> headers, int maxBody)
            throws IOException {
        String cl = headers.get("content-length");
        if (cl == null) return "";
        int n;
        try {
            n = Integer.parseInt(cl);
        } catch (NumberFormatException e) {
            throw new HttpError(400, "json");
        }
        if (n < 0) throw new HttpError(400, "json");
        if (n > maxBody) throw new HttpError(413, "size");
        byte[] b = new byte[n];
        int off = 0;
        while (off < n) {
            int r = in.read(b, off, n - off);
            if (r < 0) throw new IOException("본문이 중간에 끊김");
            off += r;
        }
        return new String(b, StandardCharsets.UTF_8);
    }

    /** 서버 쪽: 요청 한 건을 읽는다. 형식 오류는 HttpError로 던진다. */
    public static Request readRequest(InputStream in, int maxBody) throws IOException {
        return readRequest(in, maxBody, 0L);
    }

    /** deadlineNs(System.nanoTime 기준)가 0이 아니면 그 시각이 지나면 IOException (B16). */
    public static Request readRequest(InputStream raw, int maxBody, long deadlineNs) throws IOException {
        Guarded in = new Guarded(raw, deadlineNs);
        String line = readLine(in);
        if (line == null) throw new IOException("빈 연결");
        String[] parts = line.split(" ");
        if (parts.length < 2) throw new HttpError(400, "json");
        Request r = new Request();
        r.method = parts[0];
        r.path = parts[1];
        readHeaders(in, r.headers);
        in.headerDone();
        r.body = readBody(in, r.headers, maxBody);
        return r;
    }

    private static String reason(int status) {
        switch (status) {
            case 200: return "OK";
            case 400: return "Bad Request";
            case 401: return "Unauthorized";
            case 404: return "Not Found";
            case 413: return "Payload Too Large";
            case 503: return "Service Unavailable";
            default: return "Error";
        }
    }

    /** 서버 쪽: 응답을 쓴다. */
    public static void writeResponse(OutputStream out, Response resp) throws IOException {
        byte[] body = resp.body.getBytes(StandardCharsets.UTF_8);
        StringBuilder sb = new StringBuilder();
        sb.append("HTTP/1.1 ").append(resp.status).append(' ').append(reason(resp.status)).append("\r\n");
        sb.append("Content-Type: application/json\r\n");
        sb.append("Content-Length: ").append(body.length).append("\r\n");
        sb.append("Connection: close\r\n");
        for (Map.Entry<String, String> e : resp.headers.entrySet()) {
            sb.append(e.getKey()).append(": ").append(e.getValue()).append("\r\n");
        }
        sb.append("\r\n");
        out.write(sb.toString().getBytes(StandardCharsets.UTF_8));
        out.write(body);
        out.flush();
    }

    /** 클라이언트 쪽: 요청을 보내고 응답을 받는다. factory가 null이면 기본 소켓을 쓴다. */
    public static Response send(SocketFactory factory, String host, int port, String method,
            String path, Map<String, String> headers, String body, int connectMs, int readMs)
            throws IOException {
        Socket s = (factory != null) ? factory.createSocket() : new Socket();
        try {
            s.connect(new InetSocketAddress(host, port), connectMs);
            s.setSoTimeout(readMs);
            byte[] b = (body == null ? "" : body).getBytes(StandardCharsets.UTF_8);
            StringBuilder sb = new StringBuilder();
            sb.append(method).append(' ').append(path).append(" HTTP/1.1\r\n");
            sb.append("Host: ").append(host).append(':').append(port).append("\r\n");
            if (b.length > 0 || "POST".equals(method)) {
                sb.append("Content-Type: application/json\r\n");
                sb.append("Content-Length: ").append(b.length).append("\r\n");
            }
            sb.append("Connection: close\r\n");
            if (headers != null) {
                for (Map.Entry<String, String> e : headers.entrySet()) {
                    sb.append(e.getKey()).append(": ").append(e.getValue()).append("\r\n");
                }
            }
            sb.append("\r\n");
            OutputStream out = s.getOutputStream();
            out.write(sb.toString().getBytes(StandardCharsets.UTF_8));
            out.write(b);
            out.flush();

            InputStream in = s.getInputStream();
            String status = readLine(in);
            if (status == null) throw new IOException("응답 없음");
            String[] parts = status.split(" ");
            if (parts.length < 2) throw new IOException("상태 줄 오류");
            Response r = new Response();
            try {
                r.status = Integer.parseInt(parts[1]);
            } catch (NumberFormatException e) {
                throw new IOException("상태 코드 오류");
            }
            readHeaders(in, r.headers);
            if (r.headers.containsKey("content-length")) {
                r.body = readBody(in, r.headers, 4096);
            } else {
                ByteArrayOutputStream buf = new ByteArrayOutputStream();
                byte[] tmp = new byte[512];
                int n;
                while ((n = in.read(tmp)) > 0 && buf.size() < 4096) buf.write(tmp, 0, n);
                r.body = new String(buf.toByteArray(), StandardCharsets.UTF_8);
            }
            return r;
        } finally {
            try { s.close(); } catch (IOException ignored) { }
        }
    }
}
