<a id="readme-top"></a>

<!-- PROJECT LOGO -->

<br />
<div align="center">
  <a href="https://github.com/Tropiscape/DIY-Auto-Fertilizer-Unit">
    <img src="images/logo.png" alt="Logo" width="80" height="80">
  </a>

<h3 align="center">DIY Auto Fertilizer Unit</h3>

<p align="center">
    An Arduino-based automated dosing system with a single pump and is designed to simplify the process of maintaining a healthy planted aquarium.
    <br />
    <a href="https://youtu.be/4LGvgf0YLbo">Watch YouTube Video</a>
    ·
    <a href="https://github.com/Tropiscape/DIY-Auto-Fertilizer-Unit/issues/new?labels=bug&template=bug-report---.md">Report Bug</a>
  </p>
</div>

<!-- TABLE OF CONTENTS -->

<details>
  <summary>Table of Contents</summary>
  <ol>
    <li>
      <a href="#about-the-project">About The Project</a>
    </li>
    <li>
      <a href="#getting-started">Getting Started</a>
      <ul>
        <li><a href="#prerequisites">Prerequisites</a></li>
        <li><a href="#wiring">Wiring</a></li>
      </ul>
    </li>
    <li><a href="#contributing">Contributing</a></li>
    <li><a href="#license">License</a></li>
    <li><a href="#contact">Contact</a></li>
  </ol>
</details>

<!-- ABOUT THE PROJECT -->
## About The Project

<div align="center">
  <a style="border-radius:50%" height="auto" width="auto" href="https://youtu.be/4LGvgf0YLbo">
    <img src="http://img.youtube.com/vi/4LGvgf0YLbo/0.jpg" alt="YouTubeVideoLink">
  </a>
</div>

There were times when I completely forgot to fertilizer my planted aquarium. More so, if I were to go on vacation, I would need to find someone I trust to dose that tank. So, why not have a machine do it instead?! This project is very simplified as it only utilizes a single pump. 

<p align="right">(<a href="#readme-top">back to top</a>)</p>



<!-- GETTING STARTED -->
## Getting Started

### Prerequisites

- List of required hardware can be found [here](https://listium.com/@Tropiscape/113693/).

- [Arduino IDE](https://www.arduino.cc/en/software) or any other IDE's that can upload to an Arduino.

- ***RTClib***, ***Wire***, ***LiquidCrystal_I2C***, ***EEPROM***, ***avr/wdt***, and ***AceSorting*** Arduino Libraries.
  
  - In Arduino IDE: Tools → Manage Libraries → Search and Install required libraries.

<!-- Wiring -->
## Wiring

<div align="center">
  <a style="border-radius:50%" height="auto" width="auto" href="https://www.tinkercad.com/things/at2M5PfSesF-auto-fert">
    <img src="images/Circuit.png" alt="Circuit">
  </a>
</div>

<div align="center">
  <a style="border-radius:50%" height="auto" width="auto" href="https://www.tinkercad.com/things/at2M5PfSesF-auto-fert">
    <img src="images/Schematic.png" alt="Schematic">
  </a>
</div>

***Note***: *TinkerCAD does not have a component for RTCs. I used a Clock Display instead as both components are identical when it comes to wiring.*

Feel free to skip this part if you are familiar with wiring an Arduino. If you are new, please take the time to familiarize yourself with breadboards. A good tutorial can be found [here.](https://www.youtube.com/watch?v=W6mixXsn-Vc&t=14s&pp=ygUVaG93IGJyZWFkYm9hcmQgd29ya3Mg)

#### RTC + LCD Wiring

Wiring the RTC and LCD screen is very straight forward as long as you follow the Colour Legend. Both are virtually the same when it comes down to wiring.

- ![FF0000](https://placehold.co/15x15/FF0000/FF0000.png) `Power (VCC)` → `5V` 

- ![800080](https://placehold.co/15x15/800080/800080.png) `Ground (GND)` → `GND` 

- ![008000](https://placehold.co/15x15/008000/008000.png) `Serial Clock Line (SCL)` → `A5` 

- ![0000FF](https://placehold.co/15x15/0000FF/0000FF.png) `Serial Data Line (SDA)` → `A4`

<i>Example:</i>

<div align="center">
  <a style="border-radius:50%" height="auto" width="auto" href="https://www.tinkercad.com/things/at2M5PfSesF-auto-fert">
    <img src="images/RTC+LCD.png" alt="RTC + LCD Circuit">
  </a>
</div>

#### Button Wiring

*If you have a 4 pin button, you will need to use the pins that are diagonal from each other.*

We will also need to use a 10kΩ Resistor for the button.

![800080](https://placehold.co/15x15/800080/800080.png) `Ground (GND)` → `One end of the resistor` → `One pin on the button` → `GND`

![FFA500](https://placehold.co/15x15/FFA500/FFA500.png) `Input/Output` → `Other end of the resistor` → `Digital Pin 2`

<i>Example:</i>

<div align="center">
  <a style="border-radius:50%" height="auto" width="auto" href="https://www.tinkercad.com/things/at2M5PfSesF-auto-fert">
    <img src="images/ButtonCircuit.png" alt="ButtonCircuit">
  </a>
</div>

#### MOSFET + Pump Wiring

<div>
  <a style="border-radius:50%" height="auto" width="auto" href="https://oscarliang.com/how-to-use-mosfet-beginner-tutorial/">
    <img src="https://oscarliang.com/wp-content/uploads/2013/10/mosfet.jpg" alt="Circuit">
  </a>
  <br />
<i>
   <a href="https://oscarliang.com/how-to-use-mosfet-beginner-tutorial/">Source for Image</a> 
</i>
</div>
<br />

MOSFETs all have a Gate, Drain, and a Source pin. 

- **Gate**: The control pin that determines whether the MOSFET is on or off. Voltage applied here allows current to flow between the Drain and Source.

- **Drain**: The pin where the current enters the MOSFET when it is turned on.

- **Source**: The pin where the current exits the MOSFET, usually connected to ground or a negative voltage in typical setups.

However, we will need to add a Flyback Diode to protect the MOSFET from short-circuiting. *Please make sure that you have the correct orientation when installng the Flyback Diode.*

![#808080](https://placehold.co/15x15/808080/808080.png) `Grey/Marked End` → ***Positive***

![#000000](https://placehold.co/15x15/000000/000000.png) `Black/Non-Marked End` → ***Negative***

<div>
  <a style="border-radius:50%">
    <img height="200px" width="200px" src="images/FlybackDiode.png" alt="Flyback Diode">
  </a>
</div>

The wiring colour scheme is as follows:

![800080](https://placehold.co/15x15/800080/800080.png) `Ground 1 (Source)` → `GND`

![800080](https://placehold.co/15x15/800080/800080.png) `Ground 2 (From Pump)` → ![000000](https://placehold.co/15x15/000000/000000.png) `Black/Non-Marked End` → `Drain`

![FFA500](https://placehold.co/15x15/FFA500/FFA500.png) `Input/Output` → `Digital Pin 2`

![FF0000](https://placehold.co/15x15/FF0000/FF0000.png) `Power` → ![808080](https://placehold.co/15x15/808080/808080.png) `Grey/Marked End` → `Vin`

<div align="center">
  <a style="border-radius:50%" height="auto" width="auto" href="https://www.tinkercad.com/things/at2M5PfSesF-auto-fert">
    <img src="images/PumpWiring.png" alt="PumpWiring">
  </a>
</div>

<p align="right">(<a href="#readme-top">back to top</a>)</p>

<!-- CONTRIBUTING -->

## Contributing

Contributions are what make the open source community such an amazing place to learn, inspire, and create. Any contributions you make are **greatly appreciated**.

If you have a suggestion that would make this better, please fork the repo and create a pull request. You can also simply open an issue with the tag "enhancement". The same goes for any changes to the hardware and wiring.
Don't forget to give the project a star! Thanks again!

1. Fork the Project
2. Create your Feature Branch (`git checkout -b feature/AmazingFeature`)
   - Use the Hardware Branch for any hardware or wiring changes.
3. Commit your Changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the Branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

<p align="right">(<a href="#readme-top">back to top</a>)</p>

### Top contributors:

<a href="https://github.com/Tropiscape/DIY-Auto-Fertilizer-Unit/graphs/contributors">
  <img src="https://contrib.rocks/image?repo=Tropiscape/DIY-Auto-Fertilizer-Unit" alt="contrib.rocks image" />
</a>

<!-- LICENSE -->

## License

Distributed under the GNU GPLv3 License. See `LICENSE.txt` for more information.

<p align="right">(<a href="#readme-top">back to top</a>)</p>

<!-- CONTACT -->

## Contact

**YouTube:** [Tropiscape Aquatics](https://www.youtube.com/@tropiscapeaquatics) 

**Email:** TropiscapeAquatics@gmail.com

**Discord Server:** [Invite](https://discord.com/invite/NuVphNdNfC)

<p align="right">(<a href="#readme-top">back to top</a>)</p>

<!-- MARKDOWN LINKS & IMAGES -->
