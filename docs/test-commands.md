Get Status of API:

```js
curl http://HOST-IP-OR-URL:31115/health
```


Test Data-Writing to the Database through API:


```js
curl -X POST http://HOST-IP-OR-URL:31115/api/v1/measurements \
  -H "Content-Type: application/json" \
  -H "X-API-Key: CHANGE_THIS_LONG_RANDOM_API_KEY" \
  -d '{
    "device_id": "hive_scale_dual_01",
    "scale_1_weight_kg": 42.5,
    "scale_2_weight_kg": 38.2,
    "hive_1_temp_c": 34.1,
    "hive_2_temp_c": 33.7,
    "ambient_temp_c": 18.4,
    "ambient_humidity_percent": 61.2
  }'
```



