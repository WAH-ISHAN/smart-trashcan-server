// src/firebase.js

import { initializeApp } from "firebase/app";
import { getDatabase } from "firebase/database";
import { getAuth } from "firebase/auth";

// Your web app's Firebase configuration
const firebaseConfig = {
  apiKey: "AIzaSyC7J9i59Roh3fOaPMyZ2vfdrrHyj1pXNPY",
  authDomain: "iot-project-aa09f.firebaseapp.com",
  databaseURL: "https://iot-project-aa09f-default-rtdb.asia-southeast1.firebasedatabase.app",
  projectId: "iot-project-aa09f",
  storageBucket: "iot-project-aa09f.firebasestorage.app",
  messagingSenderId: "842402153818",
  appId: "1:842402153818:web:283967157513e1b9e9df63",
};

// Initialize Firebase
const app = initializeApp(firebaseConfig);

// Realtime DB + Auth
const db = getDatabase(app);
const auth = getAuth(app);

export { app, db, auth };