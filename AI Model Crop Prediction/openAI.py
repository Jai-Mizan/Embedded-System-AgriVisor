from openai import OpenAI

client = OpenAI(
  api_key="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"
)

completion = client.chat.completions.create(
  model="gpt-4o-mini",
  store=True,
  messages=[
    {"role": "user", "content": "write a haiku about ai"}
  ]
)

print(completion.choices[0].message);
