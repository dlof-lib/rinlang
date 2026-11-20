## message.layer ↔ @container.api

- `message.get/post/...` تفضّل `callApi` عند وجود `@container.api` بنفس الاسم
- `message.from_callapi` يحوّل نتيجة callApi إلى message
- `message.layer_bind` / `message.layer_info` فوق apiInfo
- حقول الحاوية (baseUrl, token, retry, mock, route) تبقى كما هي
- بدون layer: callApi وapiInfo بلا تغيير

التوثيق: `docs/message-layer-container-api.md`
