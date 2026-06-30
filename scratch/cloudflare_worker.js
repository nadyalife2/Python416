export default {
  async fetch(request, env, ctx) {
    const url = new URL(request.url);
    const path = url.pathname;

    let targetUrl = "";
    if (path.startsWith("/groq/")) {
      targetUrl = "https://api.groq.com/" + path.substring("/groq/".length) + url.search;
    } else if (path.startsWith("/gemini/")) {
      targetUrl = "https://generativelanguage.googleapis.com/" + path.substring("/gemini/".length) + url.search;
    } else if (path.startsWith("/openrouter/")) {
      targetUrl = "https://openrouter.ai/" + path.substring("/openrouter/".length) + url.search;
    } else if (path.startsWith("/google-tts/")) {
      targetUrl = "https://texttospeech.googleapis.com/" + path.substring("/google-tts/".length) + url.search;
    } else if (path.startsWith("/openai-tts/")) {
      targetUrl = "https://api.openai.com/" + path.substring("/openai-tts/".length) + url.search;
    } else if (path.startsWith("/elevenlabs-tts/")) {
      targetUrl = "https://api.elevenlabs.io/" + path.substring("/elevenlabs-tts/".length) + url.search;
    } else if (path.startsWith("/yandex-tts/")) {
      targetUrl = "https://tts.api.cloud.yandex.net/" + path.substring("/yandex-tts/".length) + url.search;
    } else if (path.startsWith("/yandex-stt/")) {
      targetUrl = "https://stt.api.cloud.yandex.net/" + path.substring("/yandex-stt/".length) + url.search;
    } else if (path.startsWith("/yandex-gpt/")) {
      targetUrl = "https://llm.api.cloud.yandex.net/" + path.substring("/yandex-gpt/".length) + url.search;
    } else {
      return new Response("Not Found", { status: 404 });
    }

    // Clone headers and remove Host
    const headers = new Headers(request.headers);
    headers.delete("Host");

    // Forward the request
    try {
      const response = await fetch(targetUrl, {
        method: request.method,
        headers: headers,
        body: request.method === "POST" ? request.body : null
      });

      // Clone response headers
      const responseHeaders = new Headers(response.headers);
      responseHeaders.delete("Transfer-Encoding");
      responseHeaders.delete("Connection");

      return new Response(response.body, {
        status: response.status,
        headers: responseHeaders
      });
    } catch (e) {
      return new Response("Bridge Error: " + e.message, { status: 500 });
    }
  }
};
