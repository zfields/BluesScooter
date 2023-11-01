// Require libraries
const axios = require('axios');

exports.handler = async (context, event, callback) => {
  // Create a new messaging response object
  const twiml = new Twilio.twiml.MessagingResponse();

  // Access the incoming text content from `event.Body`
  // (this is the inbound SMS message)
  const incomingMessage = event.Body.toLowerCase();

  // If the incoming SMS includes the motor scooter emoji (🛵),
  // use the Notehub API to send a signal to Notehub.
  // | 🛵 - U+1F6F5 |
  if (incomingMessage.includes(`\u{1F6F5}`)) {
    try {
      const response = await axios.post(
        "https://api.notefile.net",
        {
          product: "com.blues.ces",
          device: "dev:860322068096251",
          req: "hub.device.signal",
          body: {},
        },
        {
          headers: {
            "X-SESSION-TOKEN": "NTo7CcBCZ4d5bqVZIEFY3FgPXL4CrJMH",
          },
        }
      );

      // Acknowledge the SMS request
      console.log(response.data);
      twiml.message("Check out what Blues can do for you at https://blues.com");

      return callback(null, twiml);
    } catch (error) {
      // In the event of an error, return a 500 error and the error message
      console.error(error);
      return callback(error);
    }
  } else if (incomingMessage.includes('test')) {
    // great for testing a simple response!
    twiml.message('Hello there!');
  } else {
    // undefined incoming message
    twiml.message('Not sure what you meant!?');
  }

  return callback(null, twiml);
};
